//go:build windows

package main

import (
	"os/exec"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	comdlg32              = windows.NewLazySystemDLL("comdlg32.dll")
	shell32               = windows.NewLazySystemDLL("shell32.dll")
	ole32                 = windows.NewLazySystemDLL("ole32.dll")
	user32                = windows.NewLazySystemDLL("user32.dll")
	procGetOpenFileNameW  = comdlg32.NewProc("GetOpenFileNameW")
	procSHBrowseForFolder = shell32.NewProc("SHBrowseForFolderW")
	procSHGetPathFromID   = shell32.NewProc("SHGetPathFromIDListW")
	procCoTaskMemFree     = ole32.NewProc("CoTaskMemFree")
	procMessageBeep       = user32.NewProc("MessageBeep")
)

type openFileNameW struct {
	lStructSize       uint32
	hwndOwner         uintptr
	hInstance         uintptr
	lpstrFilter       *uint16
	lpstrCustomFilter *uint16
	nMaxCustFilter    uint32
	nFilterIndex      uint32
	lpstrFile         *uint16
	nMaxFile          uint32
	lpstrFileTitle    *uint16
	nMaxFileTitle     uint32
	lpstrInitialDir   *uint16
	lpstrTitle        *uint16
	flags             uint32
	nFileOffset       uint16
	nFileExtension    uint16
	lpstrDefExt       *uint16
	lCustData         uintptr
	lpfnHook          uintptr
	lpTemplateName    *uint16
	pvReserved        uintptr
	dwReserved        uint32
	flagsEx           uint32
}

// filter: pairs "Label", "*.ext;*.ext2"
func openFileDialog(owner uintptr, title string, filter []string, initialDir string) (string, bool) {
	var f []uint16
	for _, s := range filter {
		f = append(f, windows.StringToUTF16(s)...) // includes NUL
	}
	f = append(f, 0)
	buf := make([]uint16, 32768)
	t, _ := windows.UTF16PtrFromString(title)
	ofn := openFileNameW{
		hwndOwner:   owner,
		lpstrFilter: &f[0],
		lpstrFile:   &buf[0],
		nMaxFile:    uint32(len(buf)),
		lpstrTitle:  t,
		flags:       0x00080000 | 0x00001000 | 0x00000800 | 0x00000008, // EXPLORER|FILEMUSTEXIST|PATHMUSTEXIST|NOCHANGEDIR
	}
	if initialDir != "" {
		ofn.lpstrInitialDir, _ = windows.UTF16PtrFromString(initialDir)
	}
	ofn.lStructSize = uint32(unsafe.Sizeof(ofn))
	r, _, _ := procGetOpenFileNameW.Call(uintptr(unsafe.Pointer(&ofn)))
	if r == 0 {
		return "", false
	}
	return windows.UTF16ToString(buf), true
}

type browseInfoW struct {
	hwndOwner      uintptr
	pidlRoot       uintptr
	pszDisplayName *uint16
	lpszTitle      *uint16
	ulFlags        uint32
	lpfn           uintptr
	lParam         uintptr
	iImage         int32
}

func folderDialog(owner uintptr, title string) (string, bool) {
	windows.CoInitializeEx(0, windows.COINIT_APARTMENTTHREADED)
	name := make([]uint16, windows.MAX_PATH)
	t, _ := windows.UTF16PtrFromString(title)
	bi := browseInfoW{hwndOwner: owner, pszDisplayName: &name[0], lpszTitle: t,
		ulFlags: 0x0001 | 0x0040 | 0x0010} // RETURNONLYFSDIRS|NEWDIALOGSTYLE|EDITBOX
	pidl, _, _ := procSHBrowseForFolder.Call(uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 {
		return "", false
	}
	defer procCoTaskMemFree.Call(pidl)
	path := make([]uint16, 32768)
	r, _, _ := procSHGetPathFromID.Call(pidl, uintptr(unsafe.Pointer(&path[0])))
	if r == 0 {
		return "", false
	}
	return windows.UTF16ToString(path), true
}

func freeBytes(dir string) (uint64, error) {
	for dir != "" {
		p, err := windows.UTF16PtrFromString(dir)
		if err != nil {
			return 0, err
		}
		var free, total, totalFree uint64
		if err := windows.GetDiskFreeSpaceEx(p, &free, &total, &totalFree); err == nil {
			return free, nil
		}
		parent := parentDir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return 0, syscall.ENOENT
}

func openInExplorer(path string) { exec.Command("explorer.exe", path).Start() }

func openURL(url string) {
	exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
}

func beep() { procMessageBeep.Call(0x30) }

func hideConsoleAttr(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: false}
}
