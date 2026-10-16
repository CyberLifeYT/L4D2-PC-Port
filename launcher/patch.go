package main

// Patch format (l4d2-port-patch-<version>.zip):
//   patch.json            manifest: name, version, notes, files[{path,size,sha256}]
//   files/<path>          port runtime files, installed relative to the install dir
// Applying: verify every hash into <install>\.staging, back up the files it
// replaces to <install>\.backup\<old version>, move staged files into place,
// merge settings (user values kept, new keys added), write installed.json.
// A newer patch is just another zip with a higher version.

import (
	"archive/zip"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	PatchManifestName  = "patch.json"
	InstalledManifest  = "installed.json"
	DefaultRepo        = "CyberLifeYT/L4D2-PC-Port"
	SettingsFileName   = "l4d2_settings.ini"
	DefaultSettingsRel = "l4d2_settings.default.ini"
	// PatchMinLauncher is the oldest launcher that understands the patch format.
	PatchMinLauncher = "1.0.0"
)

var patchAssetRe = regexp.MustCompile(`(?i)^l4d2-port-patch-.*\.zip$`)

type PatchFile struct {
	Path   string `json:"path"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
}

type PatchManifest struct {
	Name        string      `json:"name"`
	Version     string      `json:"version"`
	BuildDate   string      `json:"build_date"`
	MinLauncher string      `json:"min_launcher"`
	Notes       string      `json:"notes"`
	Files       []PatchFile `json:"files"`
}

// CompareVersions compares versions like "1.2.10" or "v1.3.0-beta.2" and
// returns -1, 0 or 1. A pre-release sorts before its final release
// (1.3.0-beta.2 < 1.3.0), and pre-release parts compare numerically.
func CompareVersions(a, b string) int {
	coreA, preA := splitVersion(a)
	coreB, preB := splitVersion(b)
	if c := compareParts(coreA, coreB); c != 0 {
		return c
	}
	switch {
	case preA == "" && preB == "":
		return 0
	case preA == "":
		return 1
	case preB == "":
		return -1
	}
	return compareParts(preA, preB)
}

func splitVersion(v string) (core, pre string) {
	v = strings.TrimPrefix(strings.TrimPrefix(strings.TrimSpace(v), "v"), "V")
	if i := strings.IndexAny(v, "-+"); i >= 0 {
		return v[:i], strings.TrimLeft(v[i:], "-+")
	}
	return v, ""
}

func compareParts(a, b string) int {
	pa, pb := strings.FieldsFunc(a, isSep), strings.FieldsFunc(b, isSep)
	for i := 0; i < len(pa) || i < len(pb); i++ {
		var x, y string
		if i < len(pa) {
			x = pa[i]
		}
		if i < len(pb) {
			y = pb[i]
		}
		xn, xerr := strconv.Atoi(x)
		yn, yerr := strconv.Atoi(y)
		if x == "" {
			xn, xerr = 0, nil
		}
		if y == "" {
			yn, yerr = 0, nil
		}
		if xerr == nil && yerr == nil {
			if xn != yn {
				if xn < yn {
					return -1
				}
				return 1
			}
			continue
		}
		if x != y {
			if x < y {
				return -1
			}
			return 1
		}
	}
	return 0
}

func isSep(r rune) bool { return r == '.' || r == '-' || r == '_' }

func ReadPatchManifest(zipPath string) (*PatchManifest, error) {
	zr, err := zip.OpenReader(zipPath)
	if err != nil {
		return nil, fmt.Errorf("not a valid patch file (%v)", err)
	}
	defer zr.Close()
	for _, f := range zr.File {
		if f.Name == PatchManifestName {
			rc, err := f.Open()
			if err != nil {
				return nil, err
			}
			defer rc.Close()
			var m PatchManifest
			if err := json.NewDecoder(rc).Decode(&m); err != nil {
				return nil, fmt.Errorf("patch.json is invalid: %v", err)
			}
			if m.Version == "" || len(m.Files) == 0 {
				return nil, errors.New("patch.json has no version or no files")
			}
			return &m, nil
		}
	}
	return nil, errors.New("this zip is not an L4D2 port patch (no patch.json)")
}

func ReadInstalledManifest(installDir string) *PatchManifest {
	b, err := os.ReadFile(filepath.Join(installDir, InstalledManifest))
	if err != nil {
		return nil
	}
	var m PatchManifest
	if json.Unmarshal(b, &m) != nil {
		return nil
	}
	return &m
}

func safeRel(p string) (string, error) {
	clean := filepath.Clean(filepath.FromSlash(p))
	if filepath.IsAbs(clean) || strings.HasPrefix(clean, "..") || strings.Contains(clean, ":") {
		return "", fmt.Errorf("unsafe path in patch: %s", p)
	}
	return clean, nil
}

// ApplyPatch installs a patch zip into installDir.
func ApplyPatch(zipPath, installDir string, progress ProgressFn) (*PatchManifest, error) {
	m, err := ReadPatchManifest(zipPath)
	if err != nil {
		return nil, err
	}
	if m.MinLauncher != "" && CompareVersions(LauncherVersion, m.MinLauncher) < 0 {
		return nil, fmt.Errorf("patch %s needs launcher %s or newer (this is %s)", m.Version, m.MinLauncher, LauncherVersion)
	}
	zr, err := zip.OpenReader(zipPath)
	if err != nil {
		return nil, err
	}
	defer zr.Close()
	byName := map[string]*zip.File{}
	for _, f := range zr.File {
		byName[f.Name] = f
	}
	if err := os.MkdirAll(installDir, 0o755); err != nil {
		return nil, fmt.Errorf("cannot create install folder: %v", err)
	}
	staging := filepath.Join(installDir, ".staging")
	os.RemoveAll(staging)
	defer os.RemoveAll(staging)

	var total, done int64
	for _, pf := range m.Files {
		total += pf.Size
	}
	// 1. Extract + verify into staging.
	for i, pf := range m.Files {
		rel, err := safeRel(pf.Path)
		if err != nil {
			return nil, err
		}
		zf := byName["files/"+filepath.ToSlash(rel)]
		if zf == nil {
			return nil, fmt.Errorf("patch is incomplete: %s missing", pf.Path)
		}
		dst := filepath.Join(staging, rel)
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return nil, err
		}
		rc, err := zf.Open()
		if err != nil {
			return nil, err
		}
		out, err := os.Create(dst)
		if err != nil {
			rc.Close()
			return nil, err
		}
		h := sha256.New()
		n, err := io.Copy(io.MultiWriter(out, h), rc)
		rc.Close()
		out.Close()
		if err != nil {
			return nil, fmt.Errorf("extracting %s: %v", pf.Path, err)
		}
		if got := hex.EncodeToString(h.Sum(nil)); !strings.EqualFold(got, pf.SHA256) || n != pf.Size {
			return nil, fmt.Errorf("%s failed verification (download corrupted?)", pf.Path)
		}
		done += n
		progress(Progress{Stage: "Installing patch " + m.Version, Detail: fmt.Sprintf("%d / %d  %s", i+1, len(m.Files), pf.Path),
			Percent: 100 * float64(done) / float64(max64(total, 1))})
	}
	// 2. Back up what we replace, then move staged files into place.
	prev := ReadInstalledManifest(installDir)
	backup := filepath.Join(installDir, ".backup", "pre-"+m.Version)
	if prev != nil {
		backup = filepath.Join(installDir, ".backup", prev.Version)
	}
	for _, pf := range m.Files {
		rel, _ := safeRel(pf.Path)
		dst := filepath.Join(installDir, rel)
		if _, err := os.Stat(dst); err == nil {
			b := filepath.Join(backup, rel)
			os.MkdirAll(filepath.Dir(b), 0o755)
			os.Remove(b)
			if err := os.Rename(dst, b); err != nil {
				return nil, fmt.Errorf("cannot replace %s - is the game still running? (%v)", rel, err)
			}
		}
		os.MkdirAll(filepath.Dir(dst), 0o755)
		if err := os.Rename(filepath.Join(staging, rel), dst); err != nil {
			return nil, fmt.Errorf("installing %s: %v", rel, err)
		}
	}
	// 3. Settings: keep the user's values, add any new keys from the patch.
	if err := MergeSettings(installDir); err != nil {
		return nil, err
	}
	m.BuildDate = firstNonEmpty(m.BuildDate, time.Now().Format(time.RFC3339))
	b, _ := json.MarshalIndent(m, "", "  ")
	if err := os.WriteFile(filepath.Join(installDir, InstalledManifest), b, 0o644); err != nil {
		return nil, err
	}
	progress(Progress{Stage: "Patch " + m.Version + " installed", Percent: 100})
	return m, nil
}

func max64(a, b int64) int64 {
	if a > b {
		return a
	}
	return b
}

func firstNonEmpty(v ...string) string {
	for _, s := range v {
		if s != "" {
			return s
		}
	}
	return ""
}

// MakePatch builds a patch zip from a port build folder (developer tool).
func MakePatch(buildDir, version, notes, outZip string) (*PatchManifest, error) {
	ents, err := os.ReadDir(buildDir)
	if err != nil {
		return nil, err
	}
	var names []string
	for _, e := range ents {
		if e.IsDir() {
			continue
		}
		low := strings.ToLower(e.Name())
		if low == "l4d2.exe" || strings.HasSuffix(low, ".dll") {
			names = append(names, e.Name())
		}
	}
	if !contains(names, "l4d2.exe") {
		return nil, fmt.Errorf("%s has no l4d2.exe - build the port first", buildDir)
	}
	sort.Strings(names)
	m := &PatchManifest{Name: "L4D2 PC Port", Version: version, BuildDate: time.Now().UTC().Format(time.RFC3339),
		MinLauncher: PatchMinLauncher, Notes: notes}
	type item struct{ src, rel string }
	var items []item
	for _, n := range names {
		items = append(items, item{filepath.Join(buildDir, n), n})
	}
	if _, err := os.Stat(filepath.Join(buildDir, SettingsFileName)); err == nil {
		items = append(items, item{filepath.Join(buildDir, SettingsFileName), DefaultSettingsRel})
	}
	out, err := os.Create(outZip)
	if err != nil {
		return nil, err
	}
	defer out.Close()
	zw := zip.NewWriter(out)
	for _, it := range items {
		data, err := os.ReadFile(it.src)
		if err != nil {
			return nil, err
		}
		sum := sha256.Sum256(data)
		m.Files = append(m.Files, PatchFile{Path: filepath.ToSlash(it.rel), Size: int64(len(data)), SHA256: hex.EncodeToString(sum[:])})
		w, err := zw.CreateHeader(&zip.FileHeader{Name: "files/" + filepath.ToSlash(it.rel), Method: zip.Deflate, Modified: time.Now()})
		if err != nil {
			return nil, err
		}
		if _, err := w.Write(data); err != nil {
			return nil, err
		}
	}
	mb, _ := json.MarshalIndent(m, "", "  ")
	w, _ := zw.Create(PatchManifestName)
	w.Write(mb)
	return m, zw.Close()
}

func contains(list []string, s string) bool {
	for _, v := range list {
		if strings.EqualFold(v, s) {
			return true
		}
	}
	return false
}

// ---------------------------------------------------------------- GitHub
func DownloadFile(url, dst string, size int64, progress ProgressFn) error {
	client := &http.Client{} // no overall timeout for large downloads
	req, _ := http.NewRequest("GET", url, nil)
	req.Header.Set("User-Agent", "L4D2-PC-Port-Launcher/"+LauncherVersion)
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("download failed: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return fmt.Errorf("download failed: %s", resp.Status)
	}
	if size <= 0 {
		size = resp.ContentLength
	}
	tmp := dst + ".part"
	out, err := os.Create(tmp)
	if err != nil {
		return err
	}
	buf := make([]byte, 1<<20)
	var done int64
	last := time.Now()
	for {
		n, rerr := resp.Body.Read(buf)
		if n > 0 {
			if _, err := out.Write(buf[:n]); err != nil {
				out.Close()
				return err
			}
			done += int64(n)
			if time.Since(last) > 150*time.Millisecond && size > 0 {
				last = time.Now()
				progress(Progress{Stage: "Downloading patch", Detail: fmt.Sprintf("%.1f / %.1f MB", float64(done)/(1<<20), float64(size)/(1<<20)),
					Percent: 100 * float64(done) / float64(size)})
			}
		}
		if rerr == io.EOF {
			break
		}
		if rerr != nil {
			out.Close()
			os.Remove(tmp)
			return fmt.Errorf("download interrupted: %v", rerr)
		}
	}
	out.Close()
	os.Remove(dst)
	return os.Rename(tmp, dst)
}
