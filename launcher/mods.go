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
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync/atomic"
)

type ModInfo struct {
	Name    string `json:"name"`
	Enabled bool   `json:"enabled"`
	Files   int    `json:"files"`
	SizeMB  string `json:"size_mb"`
}

type modConfig struct {
	Order   []string        `json:"order"`
	Enabled map[string]bool `json:"enabled"`
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
		out = append(out, ModInfo{Name: n, Enabled: c.Enabled[n], Files: files, SizeMB: fmt.Sprintf("%.1f MB", float64(size)/(1<<20))})
	}
	return out, nil
}

// SaveMods stores the load order and enabled flags from the UI.
func SaveMods(install string, mods []ModInfo) error {
	c := modConfig{Enabled: map[string]bool{}}
	for _, m := range mods {
		c.Order = append(c.Order, m.Name)
		c.Enabled[m.Name] = m.Enabled
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
	return enabled, os.WriteFile(gi, out, 0o644)
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
		b.WriteString(nl + "\t\t\tGame\t\t\t\tmods/" + m)
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
