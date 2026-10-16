package main

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"
)

// Files the port needs to boot; used to validate both ISOs and folders.
// minIsoSize rejects files that cannot be a game disc (tests lower it).
var minIsoSize int64 = 64 << 20

var requiredGameFiles = []string{
	"default.xex",
	"bin/engine_360.dll",
	"bin/tier0_360.dll",
	"left4dead2/bin/client_360.dll",
	"left4dead2/bin/server_360.dll",
	"left4dead2/zip0.360.zip",
}

// Folders skipped when extracting (console system update data).
var skippedDiscFolders = []string{"$systemupdate"}

type Progress struct {
	Stage   string  `json:"stage"`
	Detail  string  `json:"detail"`
	Percent float64 `json:"percent"`
	Done    bool    `json:"done"`
	Error   string  `json:"error,omitempty"`
}

type ProgressFn func(Progress)

// IsoInfo is what the UI shows after the user picks an ISO.
type IsoInfo struct {
	Path      string `json:"path"`
	SizeGB    string `json:"size_gb"`
	Files     int    `json:"files"`
	TotalGB   string `json:"total_gb"`
	TitleID   string `json:"title_id"`
	Partition string `json:"partition"`
}

func gb(n int64) string { return fmt.Sprintf("%.2f GB", float64(n)/(1<<30)) }

func skipDisc(p string) bool {
	low := strings.ToLower(p)
	for _, s := range skippedDiscFolders {
		if low == s || strings.HasPrefix(low, s+"/") {
			return true
		}
	}
	return false
}

// InspectISO validates that the image is a readable Left 4 Dead 2 disc.
func InspectISO(isoPath string) (*IsoInfo, error) {
	if strings.TrimSpace(isoPath) == "" {
		return nil, errors.New("no ISO selected")
	}
	st, err := os.Stat(isoPath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, fmt.Errorf("the ISO file no longer exists: %s", isoPath)
		}
		return nil, fmt.Errorf("cannot read the ISO: %v", err)
	}
	if st.IsDir() {
		return nil, errors.New("that is a folder, not an .iso file (choose \"Extracted folder\" instead)")
	}
	if st.Size() < minIsoSize {
		return nil, fmt.Errorf("the file is too small to be a game disc (%s)", gb(st.Size()))
	}
	x, err := OpenXISO(isoPath)
	if err != nil {
		return nil, fmt.Errorf("%v. Make sure this is a 1:1 Xbox 360 disc image (.iso), not a PC/PS3 image or a compressed archive", err)
	}
	defer x.Close()
	info := &IsoInfo{Path: isoPath, SizeGB: gb(st.Size()), Partition: fmt.Sprintf("0x%X", x.base)}
	var total int64
	have := map[string]bool{}
	if err := x.Walk(func(e XEntry) error {
		if !e.Dir && !skipDisc(e.Path) {
			info.Files++
			total += int64(e.Size)
		}
		have[strings.ToLower(e.Path)] = true
		return nil
	}); err != nil {
		return nil, fmt.Errorf("the disc image is damaged: %v", err)
	}
	info.TotalGB = gb(total)
	for _, req := range requiredGameFiles {
		if !have[req] {
			return nil, fmt.Errorf("this disc does not contain %s - it is not Left 4 Dead 2 (Xbox 360)", req)
		}
	}
	xe, _ := x.Find("default.xex")
	r, err := x.Reader(xe)
	if err != nil {
		return nil, err
	}
	tid, err := XexTitleID(r)
	if err != nil {
		return nil, err
	}
	info.TitleID = fmt.Sprintf("%08X", tid)
	if tid != L4D2TitleID {
		return nil, fmt.Errorf("this is a different Xbox 360 game (title ID %08X); Left 4 Dead 2 is %08X", tid, L4D2TitleID)
	}
	return info, nil
}

// ValidateGameFolder checks an already-extracted game folder.
func ValidateGameFolder(dir string) error {
	for _, req := range requiredGameFiles {
		p := filepath.Join(dir, filepath.FromSlash(req))
		if _, err := os.Stat(p); err != nil {
			// Disc file names are case-insensitive; try a case-insensitive match.
			if !existsFold(dir, req) {
				return fmt.Errorf("missing %s in %s", req, dir)
			}
		}
	}
	f, err := os.Open(filepath.Join(dir, "default.xex"))
	if err != nil {
		return err
	}
	defer f.Close()
	tid, err := XexTitleID(f)
	if err != nil {
		return err
	}
	if tid != L4D2TitleID {
		return fmt.Errorf("default.xex is a different game (title ID %08X)", tid)
	}
	return nil
}

func existsFold(dir, rel string) bool {
	cur := dir
	for _, part := range strings.Split(rel, "/") {
		ents, err := os.ReadDir(cur)
		if err != nil {
			return false
		}
		found := false
		for _, e := range ents {
			if strings.EqualFold(e.Name(), part) {
				cur = filepath.Join(cur, e.Name())
				found = true
				break
			}
		}
		if !found {
			return false
		}
	}
	return true
}

// ExtractISO copies all game files from the disc into gameDir.
func ExtractISO(isoPath, gameDir string, cancel *atomic.Bool, progress ProgressFn) error {
	x, err := OpenXISO(isoPath)
	if err != nil {
		return err
	}
	defer x.Close()
	var files []XEntry
	var total int64
	if err := x.Walk(func(e XEntry) error {
		if !e.Dir && !skipDisc(e.Path) {
			files = append(files, e)
			total += int64(e.Size)
		}
		return nil
	}); err != nil {
		return err
	}
	free, ferr := freeBytes(filepath.Dir(gameDir))
	if ferr == nil && free < uint64(total)+(512<<20) {
		return fmt.Errorf("not enough free space: need %s, %s available on that drive", gb(total+(512<<20)), gb(int64(free)))
	}
	if err := os.MkdirAll(gameDir, 0o755); err != nil {
		return fmt.Errorf("cannot create %s: %v", gameDir, err)
	}
	var done int64
	buf := make([]byte, 4<<20)
	last := time.Now()
	for i, e := range files {
		if cancel != nil && cancel.Load() {
			return errors.New("cancelled")
		}
		dst := filepath.Join(gameDir, filepath.FromSlash(e.Path))
		if st, err := os.Stat(dst); err == nil && st.Size() == int64(e.Size) {
			done += int64(e.Size) // resume: already extracted
			continue
		}
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return err
		}
		r, err := x.Reader(e)
		if err != nil {
			return err
		}
		tmp := dst + ".part"
		out, err := os.Create(tmp)
		if err != nil {
			return fmt.Errorf("cannot write %s: %v", dst, err)
		}
		for {
			n, rerr := r.Read(buf)
			if n > 0 {
				if _, werr := out.Write(buf[:n]); werr != nil {
					out.Close()
					os.Remove(tmp)
					return fmt.Errorf("write failed (disk full?): %v", werr)
				}
				done += int64(n)
				if time.Since(last) > 120*time.Millisecond {
					last = time.Now()
					progress(Progress{Stage: "Extracting game files",
						Detail:  fmt.Sprintf("%d / %d  %s", i+1, len(files), e.Path),
						Percent: 100 * float64(done) / float64(total)})
				}
			}
			if rerr == io.EOF {
				break
			}
			if rerr != nil {
				out.Close()
				os.Remove(tmp)
				return fmt.Errorf("read failed at %s (damaged ISO?): %v", e.Path, rerr)
			}
		}
		if err := out.Close(); err != nil {
			return err
		}
		os.Remove(dst)
		if err := os.Rename(tmp, dst); err != nil {
			return err
		}
	}
	progress(Progress{Stage: "Extracting game files", Detail: fmt.Sprintf("%d files, %s", len(files), gb(total)), Percent: 100})
	return nil
}
