package main

// Shader cache builder window (l4d2-launcher.exe --shader-cache, the
// "Build shader cache" button, or "Build Shader Cache.bat" in the install
// folder). It starts the game with l4d2_precache_maps set: the game loads
// every campaign map, looks around and quits (port/src/precache.cpp), so all
// shader pipelines are compiled once into the normal shader storage. The
// window shows progress from logs\precache.json and then offers
// Delete cache / Play game / Exit.

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	webview2 "github.com/jchv/go-webview2"
	"golang.org/x/sys/windows"
)

//go:embed ui/shadercache.html
var shaderCacheHTML string

// PrecacheStatus is what the window shows.
type PrecacheStatus struct {
	State     string  `json:"state"` // idle, starting, running, done, failed, cancelled
	Index     int     `json:"index"`
	Total     int     `json:"total"`
	Map       string  `json:"map"`
	Phase     string  `json:"phase"`
	Pipelines int     `json:"pipelines"`
	Elapsed   float64 `json:"elapsed"`
	Percent   float64 `json:"percent"`
	Message   string  `json:"message"`
	CacheMB   float64 `json:"cache_mb"`
	CachePath string  `json:"cache_path"`
}

type shaderCache struct {
	dir     string
	mu      sync.Mutex
	game    *exec.Cmd
	state   string
	message string
	total   int
	started time.Time
}

// CampaignMaps lists the installed campaign maps (game\left4dead2\maps\*.360.bsp
// without the credits map), in campaign/chapter order.
func CampaignMaps(dir string) []string {
	var out []string
	ents, _ := os.ReadDir(filepath.Join(dir, "game", "left4dead2", "maps"))
	for _, e := range ents {
		n := strings.ToLower(e.Name())
		if !strings.HasSuffix(n, ".360.bsp") {
			continue
		}
		m := strings.TrimSuffix(n, ".360.bsp")
		if m == "credits" || strings.HasPrefix(m, "test") {
			continue
		}
		out = append(out, m)
	}
	sort.Strings(out)
	return out
}

// readPaths parses logs\paths.txt written by the game.
func readPaths(dir string) map[string]string {
	out := map[string]string{}
	b, err := os.ReadFile(filepath.Join(dir, "logs", "paths.txt"))
	if err != nil {
		return out
	}
	for _, l := range strings.Split(string(b), "\n") {
		if k, v, ok := strings.Cut(strings.TrimSpace(l), "="); ok {
			out[k] = v
		}
	}
	return out
}

// shaderCacheDir is the game's shader storage folder for this title.
func shaderCacheDir(dir string) string {
	root := readPaths(dir)["cache_root"]
	if root == "" {
		return ""
	}
	return root
}

func dirSizeMB(p string) float64 {
	var n int64
	filepath.WalkDir(p, func(_ string, d os.DirEntry, err error) error {
		if err == nil && !d.IsDir() {
			if fi, e := d.Info(); e == nil {
				n += fi.Size()
			}
		}
		return nil
	})
	return float64(n) / (1 << 20)
}

func (s *shaderCache) start() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.game != nil {
		return errors.New("already running")
	}
	exe := filepath.Join(s.dir, "l4d2.exe")
	if _, err := os.Stat(exe); err != nil {
		return errors.New("the port is not installed yet - install it with the launcher first")
	}
	maps := CampaignMaps(s.dir)
	if len(maps) == 0 {
		return errors.New("no campaign maps found in the install folder")
	}
	if _, err := ApplyMods(s.dir); err != nil {
		return fmt.Errorf("could not apply mods: %v", err)
	}
	// The player's own settings plus the builder's switches (later lines win).
	base, _ := os.ReadFile(filepath.Join(s.dir, SettingsFileName))
	ini := string(base) + "\r\n# --- shader cache builder ---\r\n" +
		"l4d2_precache_maps=" + strings.Join(maps, ",") + "\r\n" +
		"l4d2_mod_launch_options=-novid\r\n" +
		"l4d2_skip_press_start=true\r\n" +
		"store_shaders=true\r\n" +
		"l4d2_shader_toast=true\r\n"
	os.MkdirAll(filepath.Join(s.dir, "logs"), 0o755)
	iniPath := filepath.Join(s.dir, "logs", "precache_settings.ini")
	if err := os.WriteFile(iniPath, []byte(ini), 0o644); err != nil {
		return err
	}
	os.Remove(filepath.Join(s.dir, "logs", "precache.json"))
	cmd := exec.Command(exe, "--l4d2_settings_file="+iniPath)
	cmd.Dir = s.dir
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("could not start l4d2.exe: %v", err)
	}
	s.game, s.state, s.message, s.total, s.started = cmd, "starting", "", len(maps), time.Now()
	go func() {
		cmd.Wait()
		s.mu.Lock()
		defer s.mu.Unlock()
		s.game = nil
		st := s.readProgress()
		switch {
		case s.state == "cancelled":
		case st.State == "done":
			s.state = "done"
		default:
			s.state = "failed"
			s.message = "The game closed before the cache was finished. Shaders compiled so far are kept - run it again to continue."
		}
	}()
	return nil
}

func (s *shaderCache) readProgress() PrecacheStatus {
	var st PrecacheStatus
	b, err := os.ReadFile(filepath.Join(s.dir, "logs", "precache.json"))
	if err == nil {
		json.Unmarshal(b, &st)
	}
	return st
}

func (s *shaderCache) status() PrecacheStatus {
	s.mu.Lock()
	defer s.mu.Unlock()
	st := s.readProgress()
	if s.state == "" {
		st = PrecacheStatus{State: "idle", Total: len(CampaignMaps(s.dir))}
	} else if s.game != nil {
		if st.State == "" {
			st.State = "starting"
		}
		if st.Total == 0 {
			st.Total = s.total
		}
		st.Elapsed = time.Since(s.started).Seconds()
	} else {
		st.State = s.state
	}
	if st.Total > 0 {
		// Each map counts once loaded/looked at/compiled.
		done := float64(st.Index - 1)
		switch st.Phase {
		case "looking":
			done += 0.6
		case "compiling":
			done += 0.85
		}
		if st.State == "done" {
			done = float64(st.Total)
		}
		if done < 0 {
			done = 0
		}
		st.Percent = 100 * done / float64(st.Total)
	}
	st.Message = s.message
	st.CachePath = shaderCacheDir(s.dir)
	if st.CachePath != "" {
		st.CacheMB = dirSizeMB(st.CachePath)
	}
	return st
}

func (s *shaderCache) cancel() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.game != nil && s.game.Process != nil {
		s.state = "cancelled"
		s.message = "Cancelled. Shaders compiled so far are kept."
		s.game.Process.Kill()
	}
}

// deleteCache removes the game's shader storage. Only a folder the game
// reported as its cache root is touched.
func (s *shaderCache) deleteCache() error {
	s.mu.Lock()
	running := s.game != nil
	s.mu.Unlock()
	if running {
		return errors.New("stop the game first")
	}
	p := shaderCacheDir(s.dir)
	if p == "" {
		return errors.New("the game has not reported its cache folder yet - start the game once first")
	}
	clean := filepath.Clean(p)
	if len(clean) <= 3 || !filepath.IsAbs(clean) { // never a drive root or relative path
		return fmt.Errorf("refusing to delete %q", clean)
	}
	ents, err := os.ReadDir(clean)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	for _, e := range ents {
		if err := os.RemoveAll(filepath.Join(clean, e.Name())); err != nil {
			return err
		}
	}
	return nil
}

func runShaderCache() {
	a := &App{}
	a.loadConfig()
	if a.cfg.InstallDir == "" {
		if exe, err := os.Executable(); err == nil {
			a.cfg.InstallDir = filepath.Dir(exe)
		}
	}
	s := &shaderCache{dir: a.cfg.InstallDir}
	dataDir := filepath.Join(filepath.Dir(configPath()), "webview")
	w := webview2.NewWithOptions(webview2.WebViewOptions{
		AutoFocus: true,
		DataPath:  dataDir,
		WindowOptions: webview2.WindowOptions{
			Title: "Left 4 Dead 2 - Shader Cache", Width: 720, Height: 470, IconId: 1, Center: true,
		},
	})
	if w == nil {
		windows.MessageBox(0, windows.StringToUTF16Ptr("This tool needs the Microsoft Edge WebView2 Runtime."),
			windows.StringToUTF16Ptr("L4D2 PC Port"), windows.MB_ICONERROR)
		os.Exit(1)
	}
	defer w.Destroy()
	w.SetSize(640, 430, webview2.HintMin)
	must := func(err error) {
		if err != nil {
			panic(err)
		}
	}
	must(w.Bind("scStatus", func() PrecacheStatus { return s.status() }))
	must(w.Bind("scStart", func() error { return s.start() }))
	must(w.Bind("scCancel", func() { s.cancel() }))
	must(w.Bind("scDelete", func() error { return s.deleteCache() }))
	must(w.Bind("scPlay", func() error {
		a.cfg.InstallDir = s.dir
		if err := a.Launch(); err != nil {
			return err
		}
		go func() { time.Sleep(800 * time.Millisecond); w.Terminate() }()
		return nil
	}))
	must(w.Bind("scExit", func() {
		s.cancel()
		w.Terminate()
	}))
	w.SetHtml(shaderCacheHTML)
	w.Run()
	s.cancel()
}

// openShaderCacheWindow starts this launcher again in shader cache mode.
func openShaderCacheWindow() error {
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	return exec.Command(exe, "--shader-cache").Start()
}

// writeShaderCacheShortcut puts "Build Shader Cache.bat" in the install folder.
func writeShaderCacheShortcut(dir string) {
	exe, err := os.Executable()
	if err != nil || dir == "" {
		return
	}
	bat := "@echo off\r\nrem Compiles all of the game's shaders once so play-throughs do not stutter.\r\n" +
		"start \"\" \"" + exe + "\" --shader-cache\r\n"
	os.WriteFile(filepath.Join(dir, "Build Shader Cache.bat"), []byte(bat), 0o644)
}
