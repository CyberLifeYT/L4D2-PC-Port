package main

// Mods: loose-file overrides loaded before the game's own files.
//
// Each mod is a folder in <install>\game\mods\<name>\ laid out like the
// game's left4dead2\ folder (materials\, models\, scripts\, sound\, maps\,
// resource\ ... or its own zip0.360.zip). At launch the launcher rewrites
// left4dead2\gameinfo.xtx so every enabled mod is a "Game" search path ahead
// of the game's own paths - Source then finds the mod's copy of a file first.
// The original gameinfo.xtx is kept as gameinfo.xtx.orig and restored when no
// mods are enabled.
//
// PC .vpk addons are unpacked into a mod folder. The 360 engine reads loose
// files and .360.zip packs, not .vpk; PC-format models/textures/audio inside a
// PC addon generally need 360 versions, but scripts, configs, text and maps
// built for the 360 work.

import (
	"bytes"
	"embed"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync/atomic"
)

type ModInfo struct {
	Name        string       `json:"name"`
	Enabled     bool         `json:"enabled"`
	Files       int          `json:"files"`
	SizeMB      string       `json:"size_mb"`
	Title       string       `json:"title,omitempty"`
	Version     string       `json:"version,omitempty"`
	Author      string       `json:"author,omitempty"`
	Description string       `json:"description,omitempty"`
	Settings    []ModSetting `json:"settings,omitempty"`
	Commands    int          `json:"commands,omitempty"` // console commands the mod runs
	Launch      []string     `json:"launch_options,omitempty"`
}

// ModSetting is one control a mod.json declares. The launcher shows it on the
// Mods page and substitutes its value for {key} in the mod's commands.
type ModSetting struct {
	Key     string      `json:"key"`
	Label   string      `json:"label"`
	Type    string      `json:"type"` // slider | toggle | choice
	Min     float64     `json:"min,omitempty"`
	Max     float64     `json:"max,omitempty"`
	Step    float64     `json:"step,omitempty"`
	Unit    string      `json:"unit,omitempty"`
	Default string      `json:"default"`
	Choices [][2]string `json:"choices,omitempty"` // value, label
	Help    string      `json:"help,omitempty"`
	Value   string      `json:"value"`
}

// ModManifest is the optional mod.json at a mod folder's root.
type ModManifest struct {
	Title       string       `json:"title"`
	Version     string       `json:"version"`
	Author      string       `json:"author"`
	Description string       `json:"description"`
	Settings    []ModSetting `json:"settings"`
	// Console commands run in the game while the mod is enabled, e.g.
	// "fov_desired {fov}". Re-applied every few seconds, so they survive
	// map changes. Values from Settings replace {key}.
	Commands []string `json:"commands"`
	// Game command-line switches, e.g. "-novid". Only "-word" tokens.
	LaunchOptions []string `json:"launch_options"`
}

type modConfig struct {
	Order   []string                     `json:"order"`
	Enabled map[string]bool              `json:"enabled"`
	Values  map[string]map[string]string `json:"values,omitempty"`
}

func readManifest(dir string) (*ModManifest, error) {
	b, err := os.ReadFile(filepath.Join(dir, "mod.json"))
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	var m ModManifest
	if err := json.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("mod.json: %v", err)
	}
	return &m, nil
}

var settingKeyRe = regexp.MustCompile(`^[A-Za-z0-9_]{1,32}$`)
var launchOptRe = regexp.MustCompile(`^-[a-z0-9_]{1,40}$`)

// settingValue returns a safe value for s: numbers clamped to the slider
// range, toggles as 1/0, choices limited to the declared values.
func settingValue(s ModSetting, v string) string {
	if v == "" {
		v = s.Default
	}
	switch s.Type {
	case "slider":
		f, err := strconv.ParseFloat(strings.TrimSpace(v), 64)
		if err != nil {
			f, _ = strconv.ParseFloat(s.Default, 64)
		}
		if s.Max > s.Min {
			f = math.Max(s.Min, math.Min(s.Max, f))
		}
		if s.Step > 0 {
			f = s.Min + math.Round((f-s.Min)/s.Step)*s.Step
		}
		return strconv.FormatFloat(f, 'f', -1, 64)
	case "toggle":
		if v == "1" || strings.EqualFold(v, "true") {
			return "1"
		}
		return "0"
	case "choice":
		for _, c := range s.Choices {
			if c[0] == v {
				return v
			}
		}
		if len(s.Choices) > 0 {
			for _, c := range s.Choices {
				if c[0] == s.Default {
					return s.Default
				}
			}
			return s.Choices[0][0]
		}
	}
	return ""
}

// modCommands expands a mod's commands with its current setting values.
// Commands are single lines without ';' or quotes so a mod can only run the
// commands it lists.
func modCommands(m *ModManifest, values map[string]string) ([]string, error) {
	vals := map[string]string{}
	for _, s := range m.Settings {
		if !settingKeyRe.MatchString(s.Key) {
			return nil, fmt.Errorf("bad setting key %q", s.Key)
		}
		vals[s.Key] = settingValue(s, values[s.Key])
	}
	var out []string
	for _, c := range m.Commands {
		for k, v := range vals {
			// {key.N} is the Nth space-separated part of the value, so one
			// choice can set several variables (e.g. "255 0 0" for a colour).
			for i, part := range strings.Fields(v) {
				c = strings.ReplaceAll(c, "{"+k+"."+strconv.Itoa(i+1)+"}", part)
			}
			c = strings.ReplaceAll(c, "{"+k+"}", v)
		}
		c = strings.TrimSpace(c)
		if c == "" {
			continue
		}
		if strings.ContainsAny(c, ";\"\r\n{}|") || len(c) > 200 {
			return nil, fmt.Errorf("command %q is not allowed (one simple command per entry)", c)
		}
		out = append(out, c)
	}
	return out, nil
}

func modsDir(install string) string        { return filepath.Join(install, "game", "mods") }
func modsConfigPath(install string) string { return filepath.Join(install, "mods.json") }

var modNameRe = regexp.MustCompile(`^[A-Za-z0-9 _.\-()]{1,64}$`)

func loadModConfig(install string) modConfig {
	c := modConfig{Enabled: map[string]bool{}}
	if b, err := os.ReadFile(modsConfigPath(install)); err == nil {
		json.Unmarshal(b, &c)
	}
	if c.Enabled == nil {
		c.Enabled = map[string]bool{}
	}
	return c
}

func saveModConfig(install string, c modConfig) error {
	b, _ := json.MarshalIndent(c, "", "  ")
	return os.WriteFile(modsConfigPath(install), b, 0o644)
}

// ListMods returns mod folders in load order (first = highest priority).
func ListMods(install string) ([]ModInfo, error) {
	if install == "" {
		return nil, errors.New("install the game first")
	}
	os.MkdirAll(modsDir(install), 0o755)
	ents, err := os.ReadDir(modsDir(install))
	if err != nil {
		return nil, err
	}
	c := loadModConfig(install)
	present := map[string]bool{}
	for _, e := range ents {
		if e.IsDir() {
			present[e.Name()] = true
		}
	}
	var order []string
	seen := map[string]bool{}
	for _, n := range c.Order {
		if present[n] && !seen[n] {
			order = append(order, n)
			seen[n] = true
		}
	}
	var rest []string
	for n := range present {
		if !seen[n] {
			rest = append(rest, n)
		}
	}
	sort.Strings(rest)
	order = append(order, rest...)
	var out []ModInfo
	for _, n := range order {
		files, size := 0, int64(0)
		filepath.WalkDir(filepath.Join(modsDir(install), n), func(p string, d os.DirEntry, err error) error {
			if err == nil && !d.IsDir() {
				files++
				if fi, e := d.Info(); e == nil {
					size += fi.Size()
				}
			}
			return nil
		})
		mi := ModInfo{Name: n, Enabled: c.Enabled[n], Files: files, SizeMB: fmt.Sprintf("%.1f MB", float64(size)/(1<<20))}
		if m, err := readManifest(filepath.Join(modsDir(install), n)); err != nil {
			mi.Description = "Broken mod.json: " + err.Error()
		} else if m != nil {
			mi.Title, mi.Version, mi.Author, mi.Description = m.Title, m.Version, m.Author, m.Description
			mi.Commands = len(m.Commands)
			mi.Launch = m.LaunchOptions
			for _, st := range m.Settings {
				st.Value = settingValue(st, c.Values[n][st.Key])
				mi.Settings = append(mi.Settings, st)
			}
		}
		out = append(out, mi)
	}
	return out, nil
}

// SaveMods stores the load order and enabled flags from the UI.
func SaveMods(install string, mods []ModInfo) error {
	old := loadModConfig(install)
	c := modConfig{Enabled: map[string]bool{}, Values: old.Values}
	if c.Values == nil {
		c.Values = map[string]map[string]string{}
	}
	for _, m := range mods {
		c.Order = append(c.Order, m.Name)
		c.Enabled[m.Name] = m.Enabled
		if len(m.Settings) > 0 {
			v := map[string]string{}
			for _, s := range m.Settings {
				v[s.Key] = settingValue(s, s.Value)
			}
			c.Values[m.Name] = v
		}
	}
	return saveModConfig(install, c)
}

// AddMod copies a folder, unpacks a .zip, or extracts a .vpk into a new mod.
func AddMod(install, src string) (string, error) {
	st, err := os.Stat(src)
	if err != nil {
		return "", err
	}
	base := strings.TrimSuffix(filepath.Base(src), filepath.Ext(src))
	if st.IsDir() {
		base = filepath.Base(src)
	}
	name := sanitizeModName(base)
	dst := filepath.Join(modsDir(install), name)
	for i := 2; ; i++ {
		if _, err := os.Stat(dst); os.IsNotExist(err) {
			break
		}
		dst = filepath.Join(modsDir(install), fmt.Sprintf("%s (%d)", name, i))
	}
	os.MkdirAll(dst, 0o755)
	var cerr error
	switch {
	case st.IsDir():
		var cancel atomic.Bool
		cerr = copyTree(src, dst, &cancel, func(Progress) {})
	case strings.EqualFold(filepath.Ext(src), ".vpk"):
		_, cerr = ExtractVPK(src, dst)
	case strings.EqualFold(filepath.Ext(src), ".zip") && !strings.HasSuffix(strings.ToLower(src), ".360.zip"):
		cerr = unzipTo(src, dst)
	default:
		// A single file (for example zip0.360.zip or a .bsp): put it at the mod root.
		cerr = copyFile(src, filepath.Join(dst, filepath.Base(src)))
	}
	if cerr != nil {
		os.RemoveAll(dst)
		return "", cerr
	}
	c := loadModConfig(install)
	n := filepath.Base(dst)
	c.Order = append([]string{n}, c.Order...)
	c.Enabled[n] = true
	return n, saveModConfig(install, c)
}

func sanitizeModName(s string) string {
	var b strings.Builder
	for _, r := range s {
		if modNameRe.MatchString(string(r)) {
			b.WriteRune(r)
		} else {
			b.WriteRune('_')
		}
	}
	out := strings.TrimSpace(b.String())
	if out == "" {
		out = "mod"
	}
	if len(out) > 60 {
		out = out[:60]
	}
	return out
}

// ApplyMods writes gameinfo.xtx with enabled mods ahead of the game's search
// paths (or restores the original when none are enabled). Called before launch.
func ApplyMods(install string) ([]string, error) {
	gi := filepath.Join(install, "game", "left4dead2", "gameinfo.xtx")
	orig := gi + ".orig"
	if _, err := os.Stat(orig); os.IsNotExist(err) {
		if err := copyFile(gi, orig); err != nil {
			return nil, fmt.Errorf("cannot back up gameinfo.xtx: %v", err)
		}
	}
	src, err := os.ReadFile(orig)
	if err != nil {
		return nil, err
	}
	mods, err := ListMods(install)
	if err != nil {
		return nil, err
	}
	var enabled []string
	for _, m := range mods {
		if m.Enabled && modNameRe.MatchString(m.Name) {
			enabled = append(enabled, m.Name)
		}
	}
	out, err := injectSearchPaths(src, enabled)
	if err != nil {
		return nil, err
	}
	if err := os.WriteFile(gi, out, 0o644); err != nil {
		return nil, err
	}
	// Console commands of enabled mods -> l4d2_console_commands in the
	// settings file; the game runs them through its own console.
	c := loadModConfig(install)
	var cmds []string
	for _, n := range enabled {
		m, err := readManifest(filepath.Join(modsDir(install), n))
		if err != nil {
			return nil, fmt.Errorf("%s: %v", n, err)
		}
		if m == nil {
			continue
		}
		mc, err := modCommands(m, c.Values[n])
		if err != nil {
			return nil, fmt.Errorf("%s: %v", n, err)
		}
		cmds = append(cmds, mc...)
	}
	var opts []string
	seen := map[string]bool{}
	for _, n := range enabled {
		m, _ := readManifest(filepath.Join(modsDir(install), n))
		if m == nil {
			continue
		}
		for _, o := range m.LaunchOptions {
			o = strings.ToLower(strings.TrimSpace(o))
			if !launchOptRe.MatchString(o) {
				return nil, fmt.Errorf("%s: launch option %q is not allowed (only -switches)", n, o)
			}
			if !seen[o] {
				seen[o] = true
				opts = append(opts, o)
			}
		}
	}
	vals := ReadSettings(install)
	joined := strings.Join(cmds, ";")
	launch := strings.Join(opts, " ")
	if vals[ModCommandsKey] != joined || vals[ModLaunchKey] != launch {
		vals[ModCommandsKey] = joined
		vals[ModLaunchKey] = launch
		if err := WriteSettings(install, vals); err != nil {
			return nil, err
		}
	}
	return enabled, nil
}

// ModLaunchKey carries the mods' game command-line switches.
const ModLaunchKey = "l4d2_mod_launch_options"

// ModCommandsKey is the game setting that carries the mods' console commands.
const ModCommandsKey = "l4d2_console_commands"

//go:embed starter_mods
var starterMods embed.FS

// InstallStarterMods copies the bundled example mods (FOV Changer, Skip
// Intro) into the mods folder, enabled, without touching existing copies.
func InstallStarterMods(install string) ([]string, error) {
	if install == "" {
		return nil, errors.New("install the game first")
	}
	ents, err := starterMods.ReadDir("starter_mods")
	if err != nil {
		return nil, err
	}
	c := loadModConfig(install)
	var added []string
	for _, e := range ents {
		if !e.IsDir() {
			continue
		}
		root := "starter_mods/" + e.Name()
		var man ModManifest
		if b, err := starterMods.ReadFile(root + "/mod.json"); err == nil {
			json.Unmarshal(b, &man)
		}
		name := sanitizeModName(man.Title)
		if man.Title == "" {
			name = e.Name()
		}
		dst := filepath.Join(modsDir(install), name)
		if _, err := os.Stat(dst); err == nil {
			continue // already there; keep the user's copy
		}
		err := fs.WalkDir(starterMods, root, func(p string, d fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			rel := strings.TrimPrefix(strings.TrimPrefix(p, root), "/")
			target := filepath.Join(dst, filepath.FromSlash(rel))
			if d.IsDir() {
				return os.MkdirAll(target, 0o755)
			}
			b, err := starterMods.ReadFile(p)
			if err != nil {
				return err
			}
			return os.WriteFile(target, b, 0o644)
		})
		if err != nil {
			os.RemoveAll(dst)
			return added, err
		}
		c.Order = append(c.Order, name)
		c.Enabled[name] = true
		added = append(added, name)
	}
	return added, saveModConfig(install, c)
}

// injectSearchPaths inserts "Game mods/<name>" lines at the top of the
// SearchPaths block, keeping everything else byte-for-byte.
func injectSearchPaths(src []byte, mods []string) ([]byte, error) {
	if len(mods) == 0 {
		return src, nil
	}
	s := string(src)
	i := strings.Index(strings.ToLower(s), "searchpaths")
	if i < 0 {
		return nil, errors.New("gameinfo.xtx has no SearchPaths block")
	}
	j := strings.Index(s[i:], "{")
	if j < 0 {
		return nil, errors.New("gameinfo.xtx SearchPaths block is malformed")
	}
	at := i + j + 1
	nl := "\n"
	if strings.Contains(s, "\r\n") {
		nl = "\r\n"
	}
	var b strings.Builder
	b.WriteString(nl + "\t\t\t// Added by the L4D2 PC Port launcher (Mods page)")
	for _, m := range mods {
		// Quoted: KeyValues splits unquoted tokens at spaces ("My Addon").
		b.WriteString(nl + "\t\t\tGame\t\t\t\t\"mods/" + m + "\"")
	}
	return []byte(s[:at] + b.String() + s[at:]), nil
}

// ------------------------------------------------------------------ VPK

// ExtractVPK unpacks a single-file Valve pak (v1 or v2, as used by PC addons)
// into dst. Multi-part paks (*_dir.vpk + *_000.vpk ...) are read when the part
// files sit next to the directory file. Returns the number of files written.
func ExtractVPK(path, dst string) (int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	var hdr struct {
		Sig, Ver, TreeSize uint32
	}
	if err := binary.Read(f, binary.LittleEndian, &hdr); err != nil || hdr.Sig != 0x55AA1234 {
		return 0, errors.New("not a VPK file")
	}
	headerSize := int64(12)
	if hdr.Ver == 2 {
		headerSize = 28
	} else if hdr.Ver != 1 {
		return 0, fmt.Errorf("unsupported VPK version %d", hdr.Ver)
	}
	tree := make([]byte, hdr.TreeSize)
	if _, err := f.ReadAt(tree, headerSize); err != nil {
		return 0, fmt.Errorf("VPK directory is truncated: %v", err)
	}
	dataBase := headerSize + int64(hdr.TreeSize)
	r := bytes.NewReader(tree)
	readStr := func() (string, error) {
		var sb []byte
		for {
			c, err := r.ReadByte()
			if err != nil {
				return "", err
			}
			if c == 0 {
				return string(sb), nil
			}
			sb = append(sb, c)
		}
	}
	parts := map[uint16]*os.File{}
	defer func() {
		for _, p := range parts {
			p.Close()
		}
	}()
	partFile := func(idx uint16) (*os.File, error) {
		if p, ok := parts[idx]; ok {
			return p, nil
		}
		dir := strings.TrimSuffix(path, "_dir.vpk")
		p, err := os.Open(fmt.Sprintf("%s_%03d.vpk", dir, idx))
		if err != nil {
			return nil, fmt.Errorf("VPK part %03d is missing next to %s", idx, filepath.Base(path))
		}
		parts[idx] = p
		return p, nil
	}
	count := 0
	for {
		ext, err := readStr()
		if err != nil || ext == "" {
			break
		}
		for {
			dir, err := readStr()
			if err != nil || dir == "" {
				break
			}
			for {
				name, err := readStr()
				if err != nil || name == "" {
					break
				}
				var e struct {
					CRC          uint32
					PreloadBytes uint16
					ArchiveIndex uint16
					Offset       uint32
					Length       uint32
					Term         uint16
				}
				if err := binary.Read(r, binary.LittleEndian, &e); err != nil {
					return count, err
				}
				pre := make([]byte, e.PreloadBytes)
				if _, err := io.ReadFull(r, pre); err != nil {
					return count, err
				}
				rel := name
				if ext != " " {
					rel += "." + ext
				}
				if dir != " " {
					rel = dir + "/" + rel
				}
				clean := filepath.Clean(filepath.FromSlash(rel))
				if filepath.IsAbs(clean) || strings.HasPrefix(clean, "..") {
					return count, fmt.Errorf("VPK entry escapes the mod folder: %s", rel)
				}
				out := filepath.Join(dst, clean)
				os.MkdirAll(filepath.Dir(out), 0o755)
				data := pre
				if e.Length > 0 {
					body := make([]byte, e.Length)
					var src io.ReaderAt = f
					off := dataBase + int64(e.Offset)
					if e.ArchiveIndex != 0x7FFF {
						p, err := partFile(e.ArchiveIndex)
						if err != nil {
							return count, err
						}
						src, off = p, int64(e.Offset)
					}
					if _, err := src.ReadAt(body, off); err != nil {
						return count, fmt.Errorf("reading %s: %v", rel, err)
					}
					data = append(data, body...)
				}
				if err := os.WriteFile(out, data, 0o644); err != nil {
					return count, err
				}
				count++
			}
		}
	}
	if count == 0 {
		return 0, errors.New("the VPK contains no files")
	}
	return count, nil
}
