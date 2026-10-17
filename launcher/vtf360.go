package main

// PC -> Xbox 360 texture conversion for addons.
//
// A 360 texture (name.360.vtf) holds the same pixel data as a PC .vtf: the
// mip chain smallest to largest, each mip's frames/faces/slices in order,
// stored linearly (no GPU tiling). Differences, found from the game's own
// files:
//   * a big-endian "VTFX" header (version 0x360.8, 68 bytes, one image
//     resource) instead of the little-endian "VTF\0" header, and no low-res
//     thumbnail;
//   * DXT1/3/5 blocks byte-swapped per 16-bit word (the 360 reads them
//     big-endian); 4-byte pixel formats per 32-bit word; 2-byte formats per
//     16-bit word.
// Models (.mdl/.vvd/.vtx/.phy) and sounds (.wav/.mp3) need different 360
// encodings (big-endian studio models, XMA audio) that this does not do;
// ConvertModAssets reports them.

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io/fs"
	"math"
	"os"
	"path/filepath"
	"strings"
)

// Valve ImageFormat ids.
const (
	fmtRGBA8888 = 0
	fmtABGR8888 = 1
	fmtRGB888   = 2
	fmtBGR888   = 3
	fmtRGB565   = 4
	fmtI8       = 5
	fmtIA88     = 6
	fmtA8       = 8
	fmtARGB8888 = 11
	fmtBGRA8888 = 12
	fmtDXT1     = 13
	fmtDXT3     = 14
	fmtDXT5     = 15
	fmtBGRX8888 = 16
	fmtBGR565   = 17
	fmtBGRX5551 = 18
	fmtBGRA4444 = 19
	fmtDXT1A    = 20
	fmtBGRA5551 = 21
	fmtUV88     = 22
	fmtUVWQ8888 = 23
	fmtRGBA16F  = 24
)

// imageSize is the byte size of one w x h x d image in format f (0 if unknown).
func imageSize(f, w, h, d int) int {
	if w < 1 {
		w = 1
	}
	if h < 1 {
		h = 1
	}
	if d < 1 {
		d = 1
	}
	switch f {
	case fmtDXT1, fmtDXT1A:
		return ((w + 3) / 4) * ((h + 3) / 4) * 8 * d
	case fmtDXT3, fmtDXT5:
		return ((w + 3) / 4) * ((h + 3) / 4) * 16 * d
	}
	bpp := map[int]int{fmtRGBA8888: 4, fmtABGR8888: 4, fmtRGB888: 3, fmtBGR888: 3, fmtRGB565: 2, fmtI8: 1, fmtIA88: 2,
		fmtA8: 1, fmtARGB8888: 4, fmtBGRA8888: 4, fmtBGRX8888: 4, fmtBGR565: 2, fmtBGRX5551: 2, fmtBGRA4444: 2,
		fmtBGRA5551: 2, fmtUV88: 2, fmtUVWQ8888: 4, fmtRGBA16F: 8}[f]
	return w * h * d * bpp
}

// swapUnit is the word size whose bytes the 360 stores reversed.
func swapUnit(f int) int {
	switch f {
	case fmtDXT1, fmtDXT1A, fmtDXT3, fmtDXT5, fmtRGB565, fmtBGR565, fmtBGRX5551, fmtBGRA4444, fmtBGRA5551, fmtIA88, fmtUV88, fmtRGBA16F:
		return 2
	case fmtRGBA8888, fmtABGR8888, fmtARGB8888, fmtBGRA8888, fmtBGRX8888, fmtUVWQ8888:
		return 4
	}
	return 1
}

func swapWords(b []byte, unit int) {
	switch unit {
	case 2:
		for i := 0; i+1 < len(b); i += 2 {
			b[i], b[i+1] = b[i+1], b[i]
		}
	case 4:
		for i := 0; i+3 < len(b); i += 4 {
			b[i], b[i+1], b[i+2], b[i+3] = b[i+3], b[i+2], b[i+1], b[i]
		}
	}
}

// ConvertVTF turns a PC .vtf (7.0-7.5) into the 360 format.
func ConvertVTF(src []byte) ([]byte, error) {
	if len(src) < 64 || string(src[:4]) != "VTF\x00" {
		return nil, errors.New("not a PC VTF file")
	}
	le := binary.LittleEndian
	major, minor := le.Uint32(src[4:]), le.Uint32(src[8:])
	if major != 7 || minor > 5 {
		return nil, fmt.Errorf("unsupported VTF version %d.%d", major, minor)
	}
	headerSize := int(le.Uint32(src[12:]))
	w, h := int(le.Uint16(src[16:])), int(le.Uint16(src[18:]))
	flags := le.Uint32(src[20:])
	frames := int(le.Uint16(src[24:]))
	refl := src[32:44]
	bump := src[48:52]
	format := int(int32(le.Uint32(src[52:])))
	mips := int(src[56])
	lowFmt := int(int32(le.Uint32(src[57:])))
	lowW, lowH := int(src[61]), int(src[62])
	depth := 1
	if minor >= 2 && len(src) >= 65 {
		depth = int(le.Uint16(src[63:]))
	}
	if depth < 1 {
		depth = 1
	}
	if frames < 1 {
		frames = 1
	}
	faces, keepFaces := 1, 1
	if flags&0x4000 != 0 { // TEXTUREFLAGS_ENVMAP
		faces, keepFaces = 6, 6
		if minor < 5 && le.Uint16(src[26:]) != 0xFFFF {
			faces = 7 // pre-7.5 cubemaps carry a spheremap face, which is dropped
		}
	}
	if imageSize(format, 4, 4, 1) == 0 {
		return nil, fmt.Errorf("unsupported texture format %d", format)
	}
	// Locate the high-res image data.
	dataOff := -1
	if minor >= 3 && len(src) >= 80 {
		n := int(le.Uint32(src[68:]))
		for i := 0; i < n; i++ {
			o := 80 + i*8
			if o+8 > len(src) {
				break
			}
			if src[o] == 0x30 && src[o+1] == 0 && src[o+2] == 0 {
				dataOff = int(le.Uint32(src[o+4:]))
			}
		}
	}
	if dataOff < 0 {
		dataOff = headerSize
		if lowFmt >= 0 && lowW > 0 && lowH > 0 {
			dataOff += imageSize(lowFmt, lowW, lowH, 1)
		}
	}
	total := 0
	for m := mips - 1; m >= 0; m-- {
		mw, mh, md := maxInt(w>>m, 1), maxInt(h>>m, 1), maxInt(depth>>m, 1)
		total += imageSize(format, mw, mh, md) * frames * faces
	}
	if dataOff < 0 || dataOff+total > len(src) {
		return nil, errors.New("truncated VTF image data")
	}
	var data []byte
	if faces == keepFaces {
		data = append([]byte(nil), src[dataOff:dataOff+total]...)
	} else {
		o := dataOff
		for m := mips - 1; m >= 0; m-- {
			sz := imageSize(format, maxInt(w>>m, 1), maxInt(h>>m, 1), maxInt(depth>>m, 1))
			for f := 0; f < frames; f++ {
				for fc := 0; fc < faces; fc++ {
					if fc < keepFaces {
						data = append(data, src[o:o+sz]...)
					}
					o += sz
				}
			}
		}
	}
	// Average colour of the smallest mip for the header's low-res sample.
	sample := [4]byte{128, 128, 128, 255}
	if format == fmtDXT1 || format == fmtDXT5 || format == fmtDXT3 || format == fmtDXT1A {
		blk := data[:8]
		if format == fmtDXT5 || format == fmtDXT3 {
			blk = data[8:16]
		}
		c := le.Uint16(blk)
		sample = [4]byte{byte((c >> 11 & 31) * 255 / 31), byte((c >> 5 & 63) * 255 / 63), byte((c & 31) * 255 / 31), 255}
	}
	swapWords(data, swapUnit(format))

	be := binary.BigEndian
	hdr := make([]byte, 68)
	copy(hdr, "VTFX")
	be.PutUint32(hdr[4:], 0x360)
	be.PutUint32(hdr[8:], 8)
	be.PutUint32(hdr[12:], 68)
	be.PutUint32(hdr[16:], flags)
	be.PutUint16(hdr[20:], uint16(w))
	be.PutUint16(hdr[22:], uint16(h))
	be.PutUint16(hdr[24:], uint16(depth))
	be.PutUint16(hdr[26:], uint16(frames))
	be.PutUint16(hdr[28:], 92) // preload data size, as in the game's files
	hdr[30] = 0                 // mip skip count
	hdr[31] = 1                 // one resource: the image
	for i := 0; i < 3; i++ {
		be.PutUint32(hdr[32+i*4:], math.Float32bits(math.Float32frombits(le.Uint32(refl[i*4:]))))
	}
	be.PutUint32(hdr[44:], math.Float32bits(math.Float32frombits(le.Uint32(bump))))
	be.PutUint32(hdr[48:], uint32(format))
	copy(hdr[52:56], sample[:])
	be.PutUint32(hdr[56:], 0) // not compressed
	hdr[60] = 0x30            // VTF_LEGACY_RSRC_IMAGE
	be.PutUint32(hdr[64:], 68)
	return append(hdr, data...), nil
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

// AssetReport lists what ConvertModAssets did to a mod folder.
type AssetReport struct {
	Textures    int      `json:"textures"`
	Failed      []string `json:"failed,omitempty"`
	Unsupported int      `json:"unsupported"` // PC models/sounds left as they are
	Examples    []string `json:"examples,omitempty"`
}

// ConvertModAssets converts every PC .vtf under dir to .360.vtf (removing the
// PC file) and counts PC-only models and sounds it cannot convert. A
// conversion_report.txt is written in dir.
func ConvertModAssets(dir string) (AssetReport, error) {
	var r AssetReport
	err := filepath.WalkDir(dir, func(p string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		low := strings.ToLower(d.Name())
		switch {
		case strings.HasSuffix(low, ".vtf") && !strings.HasSuffix(low, ".360.vtf"):
			b, err := os.ReadFile(p)
			if err != nil {
				return err
			}
			out, cerr := ConvertVTF(b)
			rel, _ := filepath.Rel(dir, p)
			if cerr != nil {
				r.Failed = append(r.Failed, rel+": "+cerr.Error())
				return nil
			}
			dst := p[:len(p)-len(".vtf")] + ".360.vtf"
			if err := os.WriteFile(dst, out, 0o644); err != nil {
				return err
			}
			os.Remove(p)
			r.Textures++
		case (strings.HasSuffix(low, ".mdl") || strings.HasSuffix(low, ".vvd") || strings.HasSuffix(low, ".vtx") ||
			strings.HasSuffix(low, ".phy") || strings.HasSuffix(low, ".wav") || strings.HasSuffix(low, ".mp3")) &&
			!strings.Contains(low, ".360."):
			r.Unsupported++
			if len(r.Examples) < 5 {
				rel, _ := filepath.Rel(dir, p)
				r.Examples = append(r.Examples, rel)
			}
		}
		return nil
	})
	var b strings.Builder
	fmt.Fprintf(&b, "PC -> Xbox 360 conversion by the L4D2 PC Port launcher\r\n\r\n")
	fmt.Fprintf(&b, "Textures converted to .360.vtf: %d\r\n", r.Textures)
	for _, f := range r.Failed {
		fmt.Fprintf(&b, "  not converted: %s\r\n", f)
	}
	if r.Unsupported > 0 {
		fmt.Fprintf(&b, "\r\nPC models/sounds that the 360 game cannot load (left as they are): %d\r\n", r.Unsupported)
		for _, e := range r.Examples {
			fmt.Fprintf(&b, "  e.g. %s\r\n", e)
		}
	}
	os.WriteFile(filepath.Join(dir, "conversion_report.txt"), []byte(b.String()), 0o644)
	return r, err
}
