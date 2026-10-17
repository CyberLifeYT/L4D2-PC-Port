//go:build windows

// L4D2 PC Port launcher.
//
//	l4d2-launcher.exe                                   GUI
//	l4d2-launcher.exe --make-patch <buildDir> <version> <out.zip> [notes]
//	l4d2-launcher.exe --inspect-iso <file.iso> [report.txt]
package main

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/jchv/go-webview2"
	"golang.org/x/sys/windows"
)

const LauncherVersion = "1.2.0"

//go:embed ui/index.html
var indexHTML string

type Config struct {
	InstallDir string `json:"install_dir"`
	IsoPath    string `json:"iso_path"`
	Repo       string `json:"repo"`
	Channel    string `json:"channel"` // "release" | "prerelease"
}

type App struct {
	w        webview2.WebView
	mu       sync.Mutex
	cfg      Config
	busy     atomic.Bool
	cancel   atomic.Bool
	game     *exec.Cmd
	gameFrom time.Time
}

func configPath() string {
	dir, err := os.UserConfigDir()
	if err != nil {
		dir = "."
	}
	return filepath.Join(dir, "L4D2PCPort", "launcher.json")
}

func (a *App) loadConfig() {
	a.cfg = Config{Repo: DefaultRepo}
	if b, err := os.ReadFile(configPath()); err == nil {
		json.Unmarshal(b, &a.cfg)
	}
	if a.cfg.Repo == "" {
		a.cfg.Repo = DefaultRepo
	}
	if a.cfg.Channel != ChannelPrerelease {
		a.cfg.Channel = ChannelRelease
	}
}

func (a *App) saveConfig() {
	os.MkdirAll(filepath.Dir(configPath()), 0o755)
	b, _ := json.MarshalIndent(a.cfg, "", "  ")
	os.WriteFile(configPath(), b, 0o644)
}

func parentDir(p string) string { return filepath.Dir(p) }

func defaultInstallDir(iso string) string {
	vol := "C:"
	if iso != "" {
		if v := filepath.VolumeName(iso); v != "" {
			vol = v
		}
	}
	return vol + `\Games\L4D2 PC Port`
}

func (a *App) hwnd() uintptr { return uintptr(a.w.Window()) }

// emit calls a JS function with a JSON payload from any goroutine.
func (a *App) emit(fn string, payload any) {
	if a.w == nil {
		return
	}
	b, _ := json.Marshal(payload)
	a.w.Dispatch(func() { a.w.Eval(fmt.Sprintf("window.%s && window.%s(%s)", fn, fn, b)) })
}

// ---------------------------------------------------------------- state
type State struct {
	LauncherVersion string `json:"launcher_version"`
	InstallDir      string `json:"install_dir"`
	DefaultDir      string `json:"default_dir"`
	IsoPath         string `json:"iso_path"`
	Repo            string `json:"repo"`
	Channel         string `json:"channel"`
	PortInstalled   bool   `json:"port_installed"`
	GameInstalled   bool   `json:"game_installed"`
	GameError       string `json:"game_error,omitempty"`
	PatchVersion    string `json:"patch_version"`
	PatchDate       string `json:"patch_date"`
	PatchNotes      string `json:"patch_notes"`
	Running         bool   `json:"running"`
	Busy            bool   `json:"busy"`
}

func (a *App) state() State {
	a.mu.Lock()
	defer a.mu.Unlock()
	s := State{LauncherVersion: LauncherVersion, InstallDir: a.cfg.InstallDir, IsoPath: a.cfg.IsoPath,
		Repo: a.cfg.Repo, Channel: a.cfg.Channel, DefaultDir: defaultInstallDir(a.cfg.IsoPath), Running: a.game != nil, Busy: a.busy.Load()}
	if a.cfg.InstallDir != "" {
		if _, err := os.Stat(filepath.Join(a.cfg.InstallDir, "l4d2.exe")); err == nil {
			s.PortInstalled = true
		}
		if err := ValidateGameFolder(filepath.Join(a.cfg.InstallDir, "game")); err == nil {
			s.GameInstalled = true
		} else {
			s.GameError = err.Error()
		}
		if m := ReadInstalledManifest(a.cfg.InstallDir); m != nil {
			s.PatchVersion, s.PatchDate, s.PatchNotes = m.Version, m.BuildDate, m.Notes
		}
	}
	return s
}

// ---------------------------------------------------------------- install job
type InstallRequest struct {
	IsoPath    string `json:"iso_path"`
	GameFolder string `json:"game_folder"` // alternative to ISO: already-extracted files
	InstallDir string `json:"install_dir"`
	PatchFrom  string `json:"patch_from"` // "github" | "file" | "skip"
	PatchFile  string `json:"patch_file"`
}

func (a *App) runJob(name string, job func(p ProgressFn) error) error {
	if !a.busy.CompareAndSwap(false, true) {
		return errors.New("another operation is already running")
	}
	a.cancel.Store(false)
	go func() {
		defer a.busy.Store(false)
		progress := func(p Progress) { a.emit("onProgress", p) }
		err := job(progress)
		if err != nil {
			a.emit("onProgress", Progress{Stage: name + " failed", Error: err.Error(), Done: true})
		} else {
			a.emit("onProgress", Progress{Stage: name + " complete", Percent: 100, Done: true})
		}
		a.emit("onState", a.state())
	}()
	return nil
}

func (a *App) obtainPatch(from, file string, p ProgressFn) (string, func(), error) {
	switch from {
	case "file":
		if file == "" {
			return "", nil, errors.New("no patch file selected")
		}
		if _, err := ReadPatchManifest(file); err != nil {
			return "", nil, err
		}
		return file, func() {}, nil
	case "github":
		p(Progress{Stage: "Checking github.com/" + a.cfg.Repo + " for the latest patch"})
		rel, err := LatestRelease(a.cfg.Repo, a.cfg.Channel)
		if err != nil {
			return "", nil, err
		}
		dst := filepath.Join(os.TempDir(), rel.AssetName)
		if err := DownloadFile(rel.AssetURL, dst, rel.AssetSize, p); err != nil {
			return "", nil, err
		}
		return dst, func() { os.Remove(dst) }, nil
	}
	return "", nil, fmt.Errorf("unknown patch source %q", from)
}

func (a *App) Install(req InstallRequest) error {
	req.InstallDir = strings.TrimSpace(req.InstallDir)
	if req.InstallDir == "" {
		return errors.New("choose an install location first")
	}
	if !filepath.IsAbs(req.InstallDir) {
		return errors.New("the install location must be a full path, e.g. D:\\Games\\L4D2 PC Port")
	}
	if req.GameFolder == "" {
		if _, err := InspectISO(req.IsoPath); err != nil {
			return err
		}
	} else if err := ValidateGameFolder(req.GameFolder); err != nil {
		return err
	}
	if a.game != nil {
		return errors.New("close the game before installing")
	}
	return a.runJob("Install", func(p ProgressFn) error {
		gameDir := filepath.Join(req.InstallDir, "game")
		if req.GameFolder != "" {
			if err := copyTree(req.GameFolder, gameDir, &a.cancel, p); err != nil {
				return err
			}
		} else {
			if err := ExtractISO(req.IsoPath, gameDir, &a.cancel, p); err != nil {
				return err
			}
		}
		a.mu.Lock()
		a.cfg.InstallDir, a.cfg.IsoPath = req.InstallDir, req.IsoPath
		a.mu.Unlock()
		a.saveConfig()
		if req.PatchFrom != "skip" {
			patch, cleanup, err := a.obtainPatch(req.PatchFrom, req.PatchFile, p)
			if err != nil {
				return fmt.Errorf("game files are installed, but the patch could not be installed: %v", err)
			}
			defer cleanup()
			if _, err := ApplyPatch(patch, req.InstallDir, p); err != nil {
				return err
			}
		}
		return nil
	})
}

func (a *App) ApplyPatchFrom(from, file string) error {
	dir := a.cfg.InstallDir
	if dir == "" {
		return errors.New("install the game first")
	}
	if a.game != nil {
		return errors.New("close the game before updating")
	}
	return a.runJob("Update", func(p ProgressFn) error {
		patch, cleanup, err := a.obtainPatch(from, file, p)
		if err != nil {
			return err
		}
		defer cleanup()
		_, err = ApplyPatch(patch, dir, p)
		return err
	})
}

type UpdateInfo struct {
	Installed  string `json:"installed"`
	Latest     string `json:"latest"`
	Available  bool   `json:"available"`
	Notes      string `json:"notes"`
	PageURL    string `json:"page_url"`
	SizeMB     string `json:"size_mb"`
	Prerelease bool   `json:"prerelease"`
	Channel    string `json:"channel"`
}

func (a *App) CheckUpdate() (*UpdateInfo, error) {
	rel, err := LatestRelease(a.cfg.Repo, a.cfg.Channel)
	if err != nil {
		return nil, err
	}
	info := &UpdateInfo{Prerelease: rel.Prerelease, Channel: a.cfg.Channel, Latest: rel.Tag, Notes: rel.Notes, PageURL: rel.PageURL, SizeMB: fmt.Sprintf("%.0f MB", float64(rel.AssetSize)/(1<<20))}
	if m := ReadInstalledManifest(a.cfg.InstallDir); m != nil {
		info.Installed = m.Version
	}
	// Switching back from pre-release to release offers the newest release
	// even when it is "older" than the installed pre-release.
	info.Available = info.Installed == "" || CompareVersions(rel.Tag, info.Installed) > 0 ||
		(a.cfg.Channel == ChannelRelease && strings.Contains(info.Installed, "-") && !rel.Prerelease &&
			CompareVersions(rel.Tag, info.Installed) != 0)
	return info, nil
}

// ---------------------------------------------------------------- launch
func (a *App) Launch() error {
	dir := a.cfg.InstallDir
	exe := filepath.Join(dir, "l4d2.exe")
	if _, err := os.Stat(exe); err != nil {
		return errors.New("the port is not installed yet - open Install first")
	}
	if err := ValidateGameFolder(filepath.Join(dir, "game")); err != nil {
		return fmt.Errorf("game files are missing or incomplete (%v) - run Install again", err)
	}
	if _, err := ApplyMods(dir); err != nil {
		return fmt.Errorf("could not apply mods: %v (disable them on the Mods page)", err)
	}
	if !hasVCRuntime(dir) {
		openURL("https://aka.ms/vs/17/release/vc_redist.x64.exe")
		return errors.New("the Microsoft Visual C++ 2015-2022 runtime (x64) is not installed. Your browser is downloading it now - run the installer, then press Launch again")
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.game != nil {
		return errors.New("the game is already running")
	}
	writeShaderCacheShortcut(dir)
	cmd := exec.Command(exe)
	cmd.Dir = dir
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("could not start l4d2.exe: %v", err)
	}
	a.game, a.gameFrom = cmd, time.Now()
	go func() {
		err := cmd.Wait()
		code := 0
		if err != nil {
			if ee, ok := err.(*exec.ExitError); ok {
				code = ee.ExitCode()
			} else {
				code = -1
			}
		}
		crash := ""
		crashFile := filepath.Join(dir, "logs", "crash.txt")
		if st, err := os.Stat(crashFile); err == nil && st.ModTime().After(a.gameFrom) {
			b, _ := os.ReadFile(crashFile)
			crash = tail(string(b), 1800)
		}
		a.mu.Lock()
		a.game = nil
		a.mu.Unlock()
		a.emit("onGameExit", map[string]any{"code": code, "crash": crash})
		a.emit("onState", a.state())
	}()
	return nil
}

// hasVCRuntime reports whether vcruntime140.dll / msvcp140.dll can be found.
func hasVCRuntime(dir string) bool {
	sys := filepath.Join(os.Getenv("SystemRoot"), "System32")
	for _, n := range []string{"vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_atomic_wait.dll"} {
		_, e1 := os.Stat(filepath.Join(sys, n))
		_, e2 := os.Stat(filepath.Join(dir, n))
		if e1 != nil && e2 != nil {
			return false
		}
	}
	return true
}

func tail(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return "..." + s[len(s)-n:]
}

// ---------------------------------------------------------------- helpers

// ---------------------------------------------------------------- bindings
func (a *App) bind() {
	w := a.w
	must := func(err error) {
		if err != nil {
			panic(err)
		}
	}
	must(w.Bind("getState", func() State { return a.state() }))
	must(w.Bind("pickISO", func() string {
		p, _ := openFileDialog(a.hwnd(), "Select your Left 4 Dead 2 (Xbox 360) disc image",
			[]string{"Xbox 360 disc image (*.iso)", "*.iso", "All files", "*.*"}, filepath.Dir(a.cfg.IsoPath))
		return p
	}))
	must(w.Bind("inspectISO", func(p string) (*IsoInfo, error) { return InspectISO(p) }))
	must(w.Bind("pickGameFolder", func() (string, error) {
		p, ok := folderDialog(a.hwnd(), "Select an extracted Left 4 Dead 2 folder (contains default.xex)")
		if !ok {
			return "", nil
		}
		return p, ValidateGameFolder(p)
	}))
	must(w.Bind("pickInstallDir", func() string {
		p, ok := folderDialog(a.hwnd(), "Choose where to install the L4D2 PC Port")
		if !ok {
			return ""
		}
		// Picking an empty or unrelated folder installs into a subfolder.
		if _, err := os.Stat(filepath.Join(p, "l4d2.exe")); err != nil && !strings.EqualFold(filepath.Base(p), "L4D2 PC Port") {
			p = filepath.Join(p, "L4D2 PC Port")
		}
		return p
	}))
	must(w.Bind("setInstallDir", func(p string) (State, error) {
		if p != "" && !filepath.IsAbs(p) {
			return a.state(), errors.New("enter a full path")
		}
		a.mu.Lock()
		a.cfg.InstallDir = p
		a.mu.Unlock()
		a.saveConfig()
		return a.state(), nil
	}))
	must(w.Bind("pickPatchFile", func() string {
		p, _ := openFileDialog(a.hwnd(), "Select an L4D2 port patch (.zip)",
			[]string{"L4D2 port patch (*.zip)", "*.zip"}, "")
		return p
	}))
	must(w.Bind("listMods", func() ([]ModInfo, error) { return ListMods(a.cfg.InstallDir) }))
	must(w.Bind("saveMods", func(m []ModInfo) error {
		if a.cfg.InstallDir == "" {
			return errors.New("install the game first")
		}
		return SaveMods(a.cfg.InstallDir, m)
	}))
	must(w.Bind("addModFolder", func() (string, error) {
		if a.cfg.InstallDir == "" {
			return "", errors.New("install the game first")
		}
		p, ok := folderDialog(a.hwnd(), "Select a mod folder (laid out like left4dead2: materials, models, scripts, ...)")
		if !ok {
			return "", nil
		}
		return AddMod(a.cfg.InstallDir, p)
	}))
	must(w.Bind("addModFile", func() (string, error) {
		if a.cfg.InstallDir == "" {
			return "", errors.New("install the game first")
		}
		p, ok := openFileDialog(a.hwnd(), "Select a mod (.vpk addon, .zip of loose files, or .360.zip pack)",
			[]string{"Mods (*.vpk;*.zip)", "*.vpk;*.zip", "All files", "*.*"}, "")
		if !ok {
			return "", nil
		}
		return AddMod(a.cfg.InstallDir, p)
	}))
	must(w.Bind("installStarterMods", func() ([]string, error) { return InstallStarterMods(a.cfg.InstallDir) }))
	must(w.Bind("openModsFolder", func() {
		if a.cfg.InstallDir != "" {
			os.MkdirAll(modsDir(a.cfg.InstallDir), 0o755)
			openInExplorer(modsDir(a.cfg.InstallDir))
		}
	}))
	must(w.Bind("readPatch", func(p string) (*PatchManifest, error) { return ReadPatchManifest(p) }))
	must(w.Bind("install", func(r InstallRequest) error { return a.Install(r) }))
	must(w.Bind("cancelJob", func() { a.cancel.Store(true) }))
	must(w.Bind("setChannel", func(ch string) (State, error) {
		if ch != ChannelRelease && ch != ChannelPrerelease {
			return State{}, fmt.Errorf("unknown channel %q", ch)
		}
		a.mu.Lock()
		a.cfg.Channel = ch
		a.mu.Unlock()
		a.saveConfig()
		return a.state(), nil
	}))
	must(w.Bind("listReleases", func() ([]ReleaseInfo, error) { return ListReleases(a.cfg.Repo, a.cfg.Channel) }))
	must(w.Bind("checkUpdate", func() (*UpdateInfo, error) { return a.CheckUpdate() }))
	must(w.Bind("applyPatch", func(from, file string) error { return a.ApplyPatchFrom(from, file) }))
	must(w.Bind("getSettings", func() map[string]any {
		return map[string]any{"schema": SettingsSchema, "values": ReadSettings(a.cfg.InstallDir)}
	}))
	must(w.Bind("saveSettings", func(v map[string]string) error {
		if a.cfg.InstallDir == "" {
			return errors.New("install the game first; settings are stored with the install")
		}
		return WriteSettings(a.cfg.InstallDir, v)
	}))
	must(w.Bind("resetSettings", func() (map[string]string, error) {
		vals := map[string]string{}
		for _, s := range SettingsSchema {
			vals[s.Key] = s.Default
		}
		return vals, nil
	}))
	must(w.Bind("launch", func() error { return a.Launch() }))
	must(w.Bind("openShaderCache", func() error {
		writeShaderCacheShortcut(a.cfg.InstallDir)
		return openShaderCacheWindow()
	}))
	must(w.Bind("openInstallFolder", func() {
		if a.cfg.InstallDir != "" {
			openInExplorer(a.cfg.InstallDir)
		}
	}))
	must(w.Bind("openLogs", func() {
		if a.cfg.InstallDir != "" {
			d := filepath.Join(a.cfg.InstallDir, "logs")
			os.MkdirAll(d, 0o755)
			openInExplorer(d)
		}
	}))
	must(w.Bind("openReleases", func() { openURL("https://github.com/" + a.cfg.Repo + "/releases") }))
	must(w.Bind("openRepo", func() { openURL("https://github.com/" + a.cfg.Repo) }))
	must(w.Bind("exitLauncher", func() { a.w.Terminate() }))
}

// ---------------------------------------------------------------- CLI
func attachConsole() {
	k := windows.NewLazySystemDLL("kernel32.dll")
	k.NewProc("AttachConsole").Call(uintptr(0xFFFFFFFF)) // ATTACH_PARENT_PROCESS
	if f, err := os.OpenFile("CONOUT$", os.O_WRONLY, 0); err == nil {
		os.Stdout, os.Stderr = f, f
	}
}

func cli(args []string) (bool, int) {
	if len(args) == 0 {
		return false, 0
	}
	switch args[0] {
	case "--make-patch":
		attachConsole()
		if len(args) < 4 {
			fmt.Println("usage: l4d2-launcher --make-patch <buildDir> <version> <out.zip> [notes]")
			return true, 2
		}
		notes := ""
		if len(args) > 4 {
			notes = strings.Join(args[4:], " ")
		}
		m, err := MakePatch(args[1], args[2], notes, args[3])
		if err != nil {
			fmt.Println("make-patch failed:", err)
			return true, 1
		}
		var total int64
		for _, f := range m.Files {
			total += f.Size
			fmt.Printf("  %-40s %10d  %s\n", f.Path, f.Size, f.SHA256[:16])
		}
		summary := fmt.Sprintf("patch %s: %d files, %.1f MB -> %s\n", m.Version, len(m.Files), float64(total)/(1<<20), args[3])
		fmt.Print(summary)
		var rep strings.Builder
		for _, f := range m.Files {
			fmt.Fprintf(&rep, "%s  %d  %s\n", f.SHA256, f.Size, f.Path)
		}
		rep.WriteString(summary)
		os.WriteFile(args[3]+".txt", []byte(rep.String()), 0o644)
		return true, 0
	case "--publish":
		// --publish <patch.zip> <version> [--prerelease] [--notes "text"] [--file extra.exe] [--repo owner/name] [--log out.txt]
		attachConsole()
		if len(args) < 3 {
			fmt.Println("usage: l4d2-launcher --publish <patch.zip> <version> [--prerelease] [--notes text] [--file path] [--repo owner/name] [--log file]")
			return true, 2
		}
		opt := PublishOptions{Repo: DefaultRepo, Version: args[2], Files: []string{args[1]}}
		logPath := ""
		for i := 3; i < len(args); i++ {
			switch args[i] {
			case "--prerelease":
				opt.Prerelease = true
			case "--notes", "--file", "--repo", "--log":
				if i+1 >= len(args) {
					fmt.Println("missing value for", args[i])
					return true, 2
				}
				v := args[i+1]
				switch args[i] {
				case "--notes":
					opt.Notes = v
				case "--file":
					if v != "" {
						opt.Files = append(opt.Files, v)
					}
				case "--repo":
					opt.Repo = v
				case "--log":
					logPath = v
				}
				i++
			}
		}
		var logf *os.File
		if logPath != "" {
			logf, _ = os.Create(logPath)
			defer logf.Close()
		}
		say := func(f string, v ...any) {
			line := fmt.Sprintf(f, v...)
			fmt.Println(line)
			if logf != nil {
				logf.WriteString(line + "\r\n")
			}
		}
		if m, err := ReadPatchManifest(args[1]); err != nil {
			say("FAILED: %v", err)
			return true, 1
		} else {
			if CompareVersions(m.Version, opt.Version) != 0 {
				say("FAILED: %s is version %s, not %s", args[1], m.Version, opt.Version)
				return true, 1
			}
			if opt.Notes == "" {
				opt.Notes = m.Notes
			}
		}
		page, err := Publish(opt, say)
		if err != nil {
			say("FAILED: %v", err)
			return true, 1
		}
		say("OK: published %s", page)
		return true, 0
	case "--install":
		// Headless install, same steps as the Install page:
		//   --install <game.iso|extracted folder> <install dir> <patch.zip|github|skip> [log.txt]
		attachConsole()
		if len(args) < 4 {
			fmt.Println("usage: l4d2-launcher --install <game.iso|folder> <installDir> <patch.zip|github|github-pre|skip> [log.txt]")
			return true, 2
		}
		var logf *os.File
		if len(args) > 4 {
			logf, _ = os.Create(args[4])
			defer logf.Close()
		}
		say := func(f string, v ...any) {
			line := fmt.Sprintf(f, v...)
			fmt.Println(line)
			if logf != nil {
				logf.WriteString(line + "\r\n")
			}
		}
		last := ""
		prog := func(p Progress) {
			key := fmt.Sprintf("%s %d", p.Stage, int(p.Percent)/10)
			if key != last {
				last = key
				say("[%3.0f%%] %s  %s", p.Percent, p.Stage, p.Detail)
			}
		}
		src, dir, patch := args[1], args[2], args[3]
		if !filepath.IsAbs(dir) {
			say("FAILED: install dir must be a full path")
			return true, 1
		}
		gameDir := filepath.Join(dir, "game")
		var cancel atomic.Bool
		var err error
		if st, e := os.Stat(src); e == nil && st.IsDir() {
			if err = ValidateGameFolder(src); err == nil {
				err = copyTree(src, gameDir, &cancel, prog)
			}
		} else if _, err = InspectISO(src); err == nil {
			err = ExtractISO(src, gameDir, &cancel, prog)
		}
		if err == nil && patch != "skip" {
			zipPath := patch
			if patch == "github" || patch == "github-pre" {
				ch := ChannelRelease
				if patch == "github-pre" {
					ch = ChannelPrerelease
				}
				var rel *ReleaseInfo
				if rel, err = LatestRelease(DefaultRepo, ch); err == nil {
					zipPath = filepath.Join(os.TempDir(), rel.AssetName)
					err = DownloadFile(rel.AssetURL, zipPath, rel.AssetSize, prog)
					defer os.Remove(zipPath)
				}
			}
			if err == nil {
				var m *PatchManifest
				if m, err = ApplyPatch(zipPath, dir, prog); err == nil {
					say("patch %s installed", m.Version)
				}
			}
		}
		if err == nil {
			err = ValidateGameFolder(gameDir)
		}
		if err != nil {
			say("FAILED: %v", err)
			return true, 1
		}
		say("OK: installed to %s", dir)
		return true, 0
	case "--inspect-iso":
		attachConsole()
		if len(args) < 2 {
			fmt.Println("usage: l4d2-launcher --inspect-iso <file.iso> [report.txt]")
			return true, 2
		}
		info, err := InspectISO(args[1])
		var out string
		if err != nil {
			out = "INVALID: " + err.Error() + "\n"
		} else {
			b, _ := json.MarshalIndent(info, "", "  ")
			out = "OK\n" + string(b) + "\n"
		}
		fmt.Print(out)
		if len(args) > 2 {
			os.WriteFile(args[2], []byte(out), 0o644)
		}
		if err != nil {
			return true, 1
		}
		return true, 0
	}
	return false, 0
}

func main() {
	if len(os.Args) > 1 && os.Args[1] == "--shader-cache" {
		runShaderCache()
		return
	}
	if handled, code := cli(os.Args[1:]); handled {
		os.Exit(code)
	}
	a := &App{}
	a.loadConfig()
	dataDir := filepath.Join(filepath.Dir(configPath()), "webview")
	a.w = webview2.NewWithOptions(webview2.WebViewOptions{
		Debug:     os.Getenv("L4D2_LAUNCHER_DEBUG") != "",
		AutoFocus: true,
		DataPath:  dataDir,
		WindowOptions: webview2.WindowOptions{
			Title:  "Left 4 Dead 2 - PC Port Launcher",
			Width:  1180,
			Height: 740,
			IconId: 1,
			Center: true,
		},
	})
	if a.w == nil {
		windows.MessageBox(0, windows.StringToUTF16Ptr("This launcher needs the Microsoft Edge WebView2 Runtime.\nInstall it from https://developer.microsoft.com/microsoft-edge/webview2/ and try again."),
			windows.StringToUTF16Ptr("L4D2 PC Port"), windows.MB_ICONERROR)
		os.Exit(1)
	}
	defer a.w.Destroy()
	a.w.SetSize(1040, 660, webview2.HintMin)
	a.bind()
	a.w.SetHtml(indexHTML)
	a.w.Run()
}
