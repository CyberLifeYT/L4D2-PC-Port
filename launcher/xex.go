package main

import (
	"encoding/binary"
	"errors"
	"io"
)

const L4D2TitleID = 0x454108D4

// XexTitleID reads the title ID from an XEX2 header's execution-info block.
func XexTitleID(r io.Reader) (uint32, error) {
	hdr := make([]byte, 0x4000)
	n, _ := io.ReadFull(r, hdr)
	hdr = hdr[:n]
	if n < 24 || string(hdr[:4]) != "XEX2" {
		return 0, errors.New("default.xex is not an XEX2 executable")
	}
	count := binary.BigEndian.Uint32(hdr[0x14:])
	for i := uint32(0); i < count && 0x18+int(i)*8+8 <= len(hdr); i++ {
		key := binary.BigEndian.Uint32(hdr[0x18+i*8:])
		val := binary.BigEndian.Uint32(hdr[0x18+i*8+4:])
		if key == 0x00040006 { // execution info
			if int(val)+16 > len(hdr) {
				return 0, errors.New("XEX execution info out of range")
			}
			return binary.BigEndian.Uint32(hdr[val+12:]), nil
		}
	}
	return 0, errors.New("XEX has no execution info")
}
