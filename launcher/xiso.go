package main

// Xbox 360 game disc image (XDVDFS / "XISO") reader.
//
// Layout: the game partition starts at a format-dependent offset (XGD2
// retail discs: 0xFD90000; XGD3: 0x2080000; extracted/rebuilt XISOs: 0).
// Sector 32 of the partition holds the volume descriptor
// "MICROSOFT*XBOX*MEDIA" + root directory sector + size. Directories are
// binary trees of entries: left/right child offsets (in dwords), start
// sector, size, attributes, name.

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"strings"
)

const (
	xisoSector      = 2048
	xisoMagic       = "MICROSOFT*XBOX*MEDIA"
	xisoAttrDir     = 0x10
	xisoEmptyMarker = 0xFFFF
)

var xisoPartitionOffsets = []int64{0xFD90000, 0x2080000, 0x18300000, 0}

type XISO struct {
	f        *os.File
	size     int64
	base     int64
	rootSec  uint32
	rootSize uint32
}

type XEntry struct {
	Path   string // forward slashes, relative to disc root
	Sector uint32
	Size   uint32
	Dir    bool
}

var ErrNotXISO = errors.New("not an Xbox 360 disc image (no XDVDFS volume found)")

func OpenXISO(p string) (*XISO, error) {
	f, err := os.Open(p)
	if err != nil {
		return nil, err
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, err
	}
	x := &XISO{f: f, size: st.Size()}
	buf := make([]byte, 32)
	for _, off := range xisoPartitionOffsets {
		if off+0x10000+32 > x.size {
			continue
		}
		if _, err := f.ReadAt(buf, off+0x10000); err != nil {
			continue
		}
		if string(buf[:20]) == xisoMagic {
			x.base = off
			x.rootSec = binary.LittleEndian.Uint32(buf[20:24])
			x.rootSize = binary.LittleEndian.Uint32(buf[24:28])
			if x.rootSize == 0 || int64(x.rootSec)*xisoSector+x.base >= x.size {
				continue
			}
			return x, nil
		}
	}
	f.Close()
	return nil, ErrNotXISO
}

func (x *XISO) Close() error { return x.f.Close() }

func (x *XISO) offset(sector uint32) int64 { return x.base + int64(sector)*xisoSector }

// Walk visits every entry (directories before their contents).
func (x *XISO) Walk(fn func(e XEntry) error) error {
	return x.walkDir(x.rootSec, x.rootSize, "", 0, fn)
}

func (x *XISO) walkDir(sector, size uint32, prefix string, depth int, fn func(e XEntry) error) error {
	if depth > 64 {
		return errors.New("directory nesting too deep (corrupt image?)")
	}
	if size == 0 {
		return nil
	}
	if x.offset(sector)+int64(size) > x.size {
		return fmt.Errorf("directory %q points past end of image (truncated or corrupt ISO)", prefix)
	}
	table := make([]byte, size)
	if _, err := x.f.ReadAt(table, x.offset(sector)); err != nil {
		return fmt.Errorf("reading directory %q: %w", prefix, err)
	}
	visited := map[uint32]bool{}
	var visit func(off uint32) error
	visit = func(off uint32) error {
		pos := off * 4
		if visited[off] || int(pos)+14 > len(table) {
			return nil
		}
		visited[off] = true
		left := binary.LittleEndian.Uint16(table[pos:])
		right := binary.LittleEndian.Uint16(table[pos+2:])
		if left == xisoEmptyMarker && right == xisoEmptyMarker {
			return nil
		}
		eSector := binary.LittleEndian.Uint32(table[pos+4:])
		eSize := binary.LittleEndian.Uint32(table[pos+8:])
		attr := table[pos+12]
		nameLen := int(table[pos+13])
		if int(pos)+14+nameLen > len(table) {
			return nil
		}
		name := string(table[pos+14 : int(pos)+14+nameLen])
		if left != 0 && left != xisoEmptyMarker {
			if err := visit(uint32(left)); err != nil {
				return err
			}
		}
		if name != "" && !strings.ContainsAny(name, "/\\\x00") && name != "." && name != ".." {
			e := XEntry{Path: path.Join(prefix, name), Sector: eSector, Size: eSize, Dir: attr&xisoAttrDir != 0}
			if err := fn(e); err != nil {
				return err
			}
			if e.Dir {
				if err := x.walkDir(eSector, eSize, e.Path, depth+1, fn); err != nil {
					return err
				}
			}
		}
		if right != 0 && right != xisoEmptyMarker {
			return visit(uint32(right))
		}
		return nil
	}
	return visit(0)
}

// Reader returns a reader over one file's bytes.
func (x *XISO) Reader(e XEntry) (io.Reader, error) {
	if x.offset(e.Sector)+int64(e.Size) > x.size {
		return nil, fmt.Errorf("%s points past end of image (truncated ISO)", e.Path)
	}
	return io.NewSectionReader(x.f, x.offset(e.Sector), int64(e.Size)), nil
}

// Find returns the entry at a disc path (case-insensitive).
func (x *XISO) Find(p string) (XEntry, bool) {
	var out XEntry
	found := false
	want := strings.ToLower(p)
	stop := errors.New("stop")
	_ = x.Walk(func(e XEntry) error {
		if strings.ToLower(e.Path) == want {
			out, found = e, true
			return stop
		}
		return nil
	})
	return out, found
}
