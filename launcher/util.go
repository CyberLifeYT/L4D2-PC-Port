package main

import (
	"archive/zip"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"
)

func copyTree(src, dst string, cancel *atomic.Bool, p ProgressFn) error {
	var files []string
	var total, done int64
	err := filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() {
			files = append(files, path)
			total += info.Size()
		}
		return nil
	})
	if err != nil {
		return err
	}
	if free, err := freeBytes(filepath.Dir(dst)); err == nil && free < uint64(total) {
		return fmt.Errorf("not enough free space: need %s", gb(total))
	}
	last := time.Now()
	for i, f := range files {
		if cancel.Load() {
			return errors.New("cancelled")
		}
		rel, _ := filepath.Rel(src, f)
		out := filepath.Join(dst, rel)
		st, _ := os.Stat(f)
		if ost, err := os.Stat(out); err == nil && ost.Size() == st.Size() {
			done += st.Size()
			continue
		}
		os.MkdirAll(filepath.Dir(out), 0o755)
		if err := copyFile(f, out); err != nil {
			return err
		}
		done += st.Size()
		if time.Since(last) > 120*time.Millisecond {
			last = time.Now()
			p(Progress{Stage: "Copying game files", Detail: fmt.Sprintf("%d / %d  %s", i+1, len(files), rel), Percent: 100 * float64(done) / float64(max64(total, 1))})
		}
	}
	return nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst + ".part")
	if err != nil {
		return err
	}
	if _, err := out.ReadFrom(in); err != nil {
		out.Close()
		return err
	}
	out.Close()
	os.Remove(dst)
	return os.Rename(dst+".part", dst)
}

// unzipTo extracts a regular zip archive into dst (no paths outside dst).
func unzipTo(src, dst string) error {
	zr, err := zip.OpenReader(src)
	if err != nil {
		return fmt.Errorf("cannot open zip: %v", err)
	}
	defer zr.Close()
	for _, f := range zr.File {
		clean := filepath.Clean(filepath.FromSlash(f.Name))
		if filepath.IsAbs(clean) || strings.HasPrefix(clean, "..") {
			return fmt.Errorf("zip entry escapes the folder: %s", f.Name)
		}
		out := filepath.Join(dst, clean)
		if f.FileInfo().IsDir() {
			os.MkdirAll(out, 0o755)
			continue
		}
		os.MkdirAll(filepath.Dir(out), 0o755)
		rc, err := f.Open()
		if err != nil {
			return err
		}
		w, err := os.Create(out)
		if err != nil {
			rc.Close()
			return err
		}
		_, err = io.Copy(w, rc)
		rc.Close()
		w.Close()
		if err != nil {
			return err
		}
	}
	return nil
}
