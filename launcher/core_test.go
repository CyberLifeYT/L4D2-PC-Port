package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync/atomic"
	"testing"
)

// buildXISO writes a minimal XDVDFS image with the given files at partition offset base.
func buildXISO(t *testing.T, out string, files map[string][]byte, base int64) {
	t.Helper()
	type node struct {
		dirs  map[string]*node
		files map[string][]byte
	}
	root := &node{map[string]*node{}, map[string][]byte{}}
	for p, data := range files {
		parts := strings.Split(p, "/")
		n := root
		for _, d := range parts[:len(parts)-1] {
			if n.dirs[d] == nil {
				n.dirs[d] = &node{map[string]*node{}, map[string][]byte{}}
			}
			n = n.dirs[d]
		}
		n.files[parts[len(parts)-1]] = data
	}
	next := uint32(34)
	blobs := map[uint32][]byte{}
	alloc := func(n int) uint32 {
		s := next
		c := uint32((n + 2047) / 2048)
		if c == 0 {
			c = 1
		}
		next += c
		return s
	}
	var mkdir func(n *node) (uint32, uint32)
	mkdir = func(n *node) (uint32, uint32) {
		type ent struct {
			name      string
			sec, size uint32
			attr      byte
		}
		var names []string
		for k := range n.dirs {
			names = append(names, k)
		}
		for k := range n.files {
			names = append(names, k)
		}
		sort.Strings(names)
		var ents []ent
		for _, k := range names {
			if d, ok := n.dirs[k]; ok {
				s, z := mkdir(d)
				ents = append(ents, ent{k, s, z, 0x10})
			} else {
				data := n.files[k]
				s := alloc(len(data))
				blobs[s] = data
				ents = append(ents, ent{k, s, uint32(len(data)), 0x80})
			}
		}
		var raw [][]byte
		for _, e := range ents {
			b := make([]byte, 14+len(e.name))
			binary.LittleEndian.PutUint32(b[4:], e.sec)
			binary.LittleEndian.PutUint32(b[8:], e.size)
			b[12], b[13] = e.attr, byte(len(e.name))
			copy(b[14:], e.name)
			for len(b)%4 != 0 {
				b = append(b, 0xFF)
			}
			raw = append(raw, b)
		}
		var tbl bytes.Buffer
		off := 0
		for i, b := range raw {
			off += len(b)
			right := uint16(0)
			if i+1 < len(raw) {
				right = uint16(off / 4)
			}
			binary.LittleEndian.PutUint16(b[2:], right)
			tbl.Write(b)
		}
		s := alloc(tbl.Len())
		blobs[s] = tbl.Bytes()
		return s, uint32(tbl.Len())
	}
	rs, rz := mkdir(root)
	f, err := os.Create(out)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	f.Truncate(base + int64(next)*2048)
	vd := append([]byte(xisoMagic), make([]byte, 8)...)
	binary.LittleEndian.PutUint32(vd[20:], rs)
	binary.LittleEndian.PutUint32(vd[24:], rz)
	f.WriteAt(vd, base+32*2048)
	for s, b := range blobs {
		f.WriteAt(b, base+int64(s)*2048)
	}
}

func fakeXex(title uint32) []byte {
	h := make([]byte, 0x200)
	copy(h, "XEX2")
	binary.BigEndian.PutUint32(h[0x14:], 1)
	binary.BigEndian.PutUint32(h[0x18:], 0x00040006)
	binary.BigEndian.PutUint32(h[0x1C:], 0x100)
	binary.BigEndian.PutUint32(h[0x10C:], title)
	return h
}

func gameFiles(title uint32) map[string][]byte {
	m := map[string][]byte{"default.xex": fakeXex(title), "$SystemUpdate/su.bin": []byte("x")}
	for _, p := range requiredGameFiles {
		if p != "default.xex" {
			m[p] = []byte(strings.Repeat(p, 50))
		}
	}
	return m
}

func TestInspectISO(t *testing.T) {
	minIsoSize = 0
	d := t.TempDir()
	good := filepath.Join(d, "good.iso")
	buildXISO(t, good, gameFiles(L4D2TitleID), 0)
	xgd2 := filepath.Join(d, "xgd2.iso")
	buildXISO(t, xgd2, gameFiles(L4D2TitleID), 0xFD90000)
	for _, p := range []string{good, xgd2} {
		info, err := InspectISO(p)
		if err != nil || info.TitleID != "454108D4" {
			t.Fatalf("%s: %v %+v", p, err, info)
		}
	}
	wrong := filepath.Join(d, "wrong.iso")
	buildXISO(t, wrong, gameFiles(0x4D5307E6), 0)
	miss := gameFiles(L4D2TitleID)
	delete(miss, "left4dead2/zip0.360.zip")
	missing := filepath.Join(d, "missing.iso")
	buildXISO(t, missing, miss, 0)
	junk := filepath.Join(d, "junk.iso")
	os.WriteFile(junk, make([]byte, 300000), 0o644)
	cases := map[string]string{
		wrong: "different Xbox 360 game", missing: "does not contain", junk: "not an Xbox 360 disc",
		filepath.Join(d, "nope.iso"): "no longer exists", d: "folder",
	}
	for p, want := range cases {
		if _, err := InspectISO(p); err == nil || !strings.Contains(err.Error(), want) {
			t.Fatalf("%s: got %v, want %q", p, err, want)
		}
	}
	minIsoSize = 64 << 20
	if _, err := InspectISO(good); err == nil || !strings.Contains(err.Error(), "too small") {
		t.Fatalf("small file accepted: %v", err)
	}
}

func TestInstallAndPatch(t *testing.T) {
	minIsoSize = 0
	d := t.TempDir()
	iso := filepath.Join(d, "g.iso")
	buildXISO(t, iso, gameFiles(L4D2TitleID), 0xFD90000)
	game := filepath.Join(d, "inst", "game")
	var cancel atomic.Bool
	if err := ExtractISO(iso, game, &cancel, func(Progress) {}); err != nil {
		t.Fatal(err)
	}
	if err := ValidateGameFolder(game); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(game, "$SystemUpdate")); err == nil {
		t.Fatal("$SystemUpdate should be skipped")
	}
	build := filepath.Join(d, "build")
	os.MkdirAll(build, 0o755)
	os.WriteFile(filepath.Join(build, "l4d2.exe"), []byte("EXE1"), 0o644)
	os.WriteFile(filepath.Join(build, "x.dll"), []byte("DLL1"), 0o644)
	os.WriteFile(filepath.Join(build, SettingsFileName), []byte("[display]\nvsync=true\n"), 0o644)
	inst := filepath.Join(d, "inst")
	z1 := filepath.Join(d, "p1.zip")
	if _, err := MakePatch(build, "1.0.0", "", z1); err != nil {
		t.Fatal(err)
	}
	if _, err := ApplyPatch(z1, inst, func(Progress) {}); err != nil {
		t.Fatal(err)
	}
	v := ReadSettings(inst)
	v["vsync"] = "false"
	if err := WriteSettings(inst, v); err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(build, "l4d2.exe"), []byte("EXE2"), 0o644)
	z2 := filepath.Join(d, "p2.zip")
	MakePatch(build, "1.0.1", "", z2)
	if _, err := ApplyPatch(z2, inst, func(Progress) {}); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(filepath.Join(inst, "l4d2.exe")); string(b) != "EXE2" {
		t.Fatal("exe not updated")
	}
	if ReadSettings(inst)["vsync"] != "false" {
		t.Fatal("player setting overwritten by patch")
	}
	if m := ReadInstalledManifest(inst); m == nil || m.Version != "1.0.1" {
		t.Fatal("installed.json not updated")
	}
	zb, _ := os.ReadFile(z2)
	zb[len(zb)/2] ^= 0xFF
	bad := filepath.Join(d, "bad.zip")
	os.WriteFile(bad, zb, 0o644)
	if _, err := ApplyPatch(bad, inst, func(Progress) {}); err == nil {
		t.Fatal("corrupt patch accepted")
	}
	if b, _ := os.ReadFile(filepath.Join(inst, "l4d2.exe")); string(b) != "EXE2" {
		t.Fatal("install damaged by a rejected patch")
	}
	if CompareVersions("1.0.10", "1.0.9") <= 0 || CompareVersions("v1.2", "1.2.0") != 0 {
		t.Fatal("version compare")
	}
}

func TestVersionsAndChannels(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.0.10", "1.0.9", 1}, {"v1.2", "1.2.0", 0}, {"1.1.0-beta.1", "1.1.0", -1},
		{"1.1.0-beta.2", "1.1.0-beta.10", -1}, {"1.1.0-beta.1", "1.0.9", 1}, {"1.1.0-rc.1", "1.1.0-beta.3", 1},
	}
	for _, c := range cases {
		if got := CompareVersions(c.a, c.b); got != c.want {
			t.Fatalf("CompareVersions(%q,%q)=%d want %d", c.a, c.b, got, c.want)
		}
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`[
		 {"tag_name":"v1.1.0-beta.1","prerelease":true,"assets":[{"name":"l4d2-port-patch-1.1.0-beta.1.zip","browser_download_url":"x","size":1}]},
		 {"tag_name":"v1.0.1","prerelease":false,"assets":[{"name":"l4d2-port-patch-1.0.1.zip","browser_download_url":"y","size":1}]},
		 {"tag_name":"v1.0.2","draft":true,"assets":[{"name":"l4d2-port-patch-1.0.2.zip","browser_download_url":"z","size":1}]},
		 {"tag_name":"v0.9.0","prerelease":false,"assets":[{"name":"notes.txt","browser_download_url":"w","size":1}]}]`))
	}))
	defer srv.Close()
	old := githubAPI
	githubAPI = srv.URL
	defer func() { githubAPI = old }()
	if r, err := LatestRelease("a/b", ChannelRelease); err != nil || r.Tag != "v1.0.1" {
		t.Fatalf("release channel: %+v %v", r, err)
	}
	if r, err := LatestRelease("a/b", ChannelPrerelease); err != nil || r.Tag != "v1.1.0-beta.1" {
		t.Fatalf("prerelease channel: %+v %v", r, err)
	}
}

func TestPublish(t *testing.T) {
	var created, uploaded, deleted int
	var gotAuth string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotAuth = r.Header.Get("Authorization")
		switch {
		case r.Method == "GET" && strings.HasSuffix(r.URL.Path, "/releases/tags/v1.1.0-beta.1"):
			w.WriteHeader(404)
		case r.Method == "GET" && strings.HasSuffix(r.URL.Path, "/releases/tags/v1.0.1"):
			w.Write([]byte(`{"id":7,"html_url":"page7","assets":[{"id":99,"name":"p.zip"}]}`))
		case r.Method == "POST" && strings.HasSuffix(r.URL.Path, "/releases"):
			created++
			w.WriteHeader(201)
			w.Write([]byte(`{"id":5,"html_url":"page5","prerelease":true}`))
		case r.Method == "DELETE":
			deleted++
			w.WriteHeader(204)
		case r.Method == "POST" && strings.Contains(r.URL.Path, "/assets"):
			b, _ := io.ReadAll(r.Body)
			if string(b) != "ZIPDATA" {
				w.WriteHeader(400)
				return
			}
			uploaded++
			w.WriteHeader(201)
			w.Write([]byte(`{}`))
		default:
			w.WriteHeader(500)
		}
	}))
	defer srv.Close()
	oa, ou := githubAPI, githubUploads
	githubAPI, githubUploads = srv.URL, srv.URL
	defer func() { githubAPI, githubUploads = oa, ou }()
	t.Setenv("GITHUB_TOKEN", "tok")
	zip := filepath.Join(t.TempDir(), "p.zip")
	os.WriteFile(zip, []byte("ZIPDATA"), 0o644)
	say := func(string, ...any) {}
	if page, err := Publish(PublishOptions{Repo: "a/b", Version: "1.1.0-beta.1", Prerelease: true, Files: []string{zip}}, say); err != nil || page != "page5" {
		t.Fatalf("new prerelease: %q %v", page, err)
	}
	if page, err := Publish(PublishOptions{Repo: "a/b", Version: "1.0.1", Files: []string{zip}}, say); err != nil || page != "page7" {
		t.Fatalf("existing release: %q %v", page, err)
	}
	if created != 1 || uploaded != 2 || deleted != 1 || gotAuth != "Bearer tok" {
		t.Fatalf("created=%d uploaded=%d deleted=%d auth=%q", created, uploaded, deleted, gotAuth)
	}
}

func buildVPK(t *testing.T, path string, files map[string][]byte) {
	t.Helper()
	type ent struct {
		ext, dir, name string
		data           []byte
	}
	var tree bytes.Buffer
	byExt := map[string]map[string][]ent{}
	for p, d := range files {
		dir, file := filepath.Split(p)
		dir = strings.TrimSuffix(dir, "/")
		if dir == "" {
			dir = " "
		}
		ext := filepath.Ext(file)
		name := strings.TrimSuffix(file, ext)
		ext = strings.TrimPrefix(ext, ".")
		if byExt[ext] == nil {
			byExt[ext] = map[string][]ent{}
		}
		byExt[ext][dir] = append(byExt[ext][dir], ent{ext, dir, name, d})
	}
	var data bytes.Buffer
	for ext, dirs := range byExt {
		tree.WriteString(ext + "\x00")
		for dir, es := range dirs {
			tree.WriteString(dir + "\x00")
			for _, e := range es {
				tree.WriteString(e.name + "\x00")
				binary.Write(&tree, binary.LittleEndian, struct {
					CRC       uint32
					Pre, Arch uint16
					Off, Len  uint32
					Term      uint16
				}{0, 0, 0x7FFF, uint32(data.Len()), uint32(len(e.data)), 0xFFFF})
				data.Write(e.data)
			}
			tree.WriteString("\x00")
		}
		tree.WriteString("\x00")
	}
	tree.WriteString("\x00")
	var out bytes.Buffer
	binary.Write(&out, binary.LittleEndian, []uint32{0x55AA1234, 1, uint32(tree.Len())})
	out.Write(tree.Bytes())
	out.Write(data.Bytes())
	os.WriteFile(path, out.Bytes(), 0o644)
}

func TestMods(t *testing.T) {
	inst := t.TempDir()
	gdir := filepath.Join(inst, "game", "left4dead2")
	os.MkdirAll(gdir, 0o755)
	orig := "\"GameInfo\"\r\n{\r\n\tFileSystem\r\n\t{\r\n\t\tSearchPaths\r\n\t\t{\r\n\t\t\tGame\t|gameinfo_path|.\r\n\t\t\tGame\tleft4dead2\r\n\t\t}\r\n\t}\r\n}\r\n"
	os.WriteFile(filepath.Join(gdir, "gameinfo.xtx"), []byte(orig), 0o644)
	vpk := filepath.Join(t.TempDir(), "My Addon.vpk")
	buildVPK(t, vpk, map[string][]byte{"scripts/vscripts/test.nut": []byte("printl(1)"), "addoninfo.txt": []byte("info"), "materials/a/b.vmt": []byte("vmt")})
	name, err := AddMod(inst, vpk)
	if err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(filepath.Join(modsDir(inst), name, "scripts", "vscripts", "test.nut")); string(b) != "printl(1)" {
		t.Fatal("vpk not extracted")
	}
	folder := filepath.Join(t.TempDir(), "Loose")
	os.MkdirAll(filepath.Join(folder, "maps"), 0o755)
	os.WriteFile(filepath.Join(folder, "maps", "x.bsp"), []byte("bsp"), 0o644)
	if _, err := AddMod(inst, folder); err != nil {
		t.Fatal(err)
	}
	mods, _ := ListMods(inst)
	if len(mods) != 2 || mods[0].Name != "Loose" || !mods[1].Enabled {
		t.Fatalf("mods %+v", mods)
	}
	enabled, err := ApplyMods(inst)
	if err != nil || len(enabled) != 2 {
		t.Fatal(enabled, err)
	}
	gi, _ := os.ReadFile(filepath.Join(gdir, "gameinfo.xtx"))
	s := string(gi)
	if !strings.Contains(s, "Game\t\t\t\t\"mods/Loose\"\r\n") || !strings.Contains(s, "\"mods/My Addon\"") || strings.Index(s, "mods/Loose") > strings.Index(s, "mods/My Addon") ||
		strings.Index(s, "mods/My Addon") > strings.Index(s, "|gameinfo_path|") {
		t.Fatalf("bad gameinfo:\n%s", s)
	}
	mods[0].Enabled, mods[1].Enabled = false, false
	SaveMods(inst, mods)
	ApplyMods(inst)
	if gi, _ := os.ReadFile(filepath.Join(gdir, "gameinfo.xtx")); string(gi) != orig {
		t.Fatal("gameinfo not restored")
	}
	if _, err := ExtractVPK(filepath.Join(gdir, "gameinfo.xtx"), t.TempDir()); err == nil {
		t.Fatal("non-vpk accepted")
	}
}

func TestStarterModsAndCommands(t *testing.T) {
	inst := t.TempDir()
	gdir := filepath.Join(inst, "game", "left4dead2")
	os.MkdirAll(gdir, 0o755)
	os.WriteFile(filepath.Join(gdir, "gameinfo.xtx"), []byte("\"GameInfo\"\n{\n\tSearchPaths\n\t{\n\t\tGame\tleft4dead2\n\t}\n}\n"), 0o644)
	added, err := InstallStarterMods(inst)
	if err != nil || len(added) != 7 {
		t.Fatal(added, err)
	}
	if again, _ := InstallStarterMods(inst); len(again) != 0 {
		t.Fatal("starter mods installed twice", again)
	}
	if _, err := os.Stat(filepath.Join(modsDir(inst), "Skip Intro", "mod.json")); err != nil {
		t.Fatal("skip intro", err)
	}
	mods, _ := ListMods(inst)
	var fov *ModInfo
	for i := range mods {
		if mods[i].Title == "FOV Changer" {
			fov = &mods[i]
		}
	}
	if fov == nil || len(fov.Settings) != 1 || fov.Settings[0].Value != "90" || !fov.Enabled || fov.Commands != 1 {
		t.Fatalf("fov mod %+v", fov)
	}
	if _, err := ApplyMods(inst); err != nil {
		t.Fatal(err)
	}
	v := ReadSettings(inst)
	for _, want := range []string{"l4d2_fov 90", "l4d2_skip_press_start 1", "cl_crosshair_red 138", "cl_crosshair_blue 220",
		"sb_combat_saccade_speed 1000", "sb_friend_immobilized_reaction_time_expert 0.5", "mat_grain_enable 0",
		"cl_viewmodelfovsurvivor 51", "sv_unlockedchapters 99"} {
		if !strings.Contains(";"+v[ModCommandsKey]+";", ";"+want+";") {
			t.Fatalf("commands %q missing %q", v[ModCommandsKey], want)
		}
	}
	if v[ModLaunchKey] != "-novid" {
		t.Fatalf("launch %q", v[ModLaunchKey])
	}
	fov.Settings[0].Value = "200" // clamped to the slider's max
	if err := SaveMods(inst, mods); err != nil {
		t.Fatal(err)
	}
	ApplyMods(inst)
	if v := ReadSettings(inst)[ModCommandsKey]; !strings.Contains(v, "l4d2_fov 120;") {
		t.Fatalf("commands %q", v)
	}
	// Settings the user saved elsewhere survive.
	vals := ReadSettings(inst)
	vals["l4d2_key_fire"] = "LMB,E"
	WriteSettings(inst, vals)
	for i := range mods {
		mods[i].Enabled = false
	}
	SaveMods(inst, mods)
	ApplyMods(inst)
	vals = ReadSettings(inst)
	if vals[ModCommandsKey] != "" || vals[ModLaunchKey] != "" || vals["l4d2_key_fire"] != "LMB,E" {
		t.Fatalf("after disable: %q %q", vals[ModCommandsKey], vals["l4d2_key_fire"])
	}
	// A mod can't smuggle extra commands through a setting value or template.
	bad := &ModManifest{Commands: []string{"say hi; quit"}}
	if _, err := modCommands(bad, nil); err == nil {
		t.Fatal("expected rejection")
	}
	ch := &ModManifest{Settings: []ModSetting{{Key: "m", Type: "choice", Default: "a", Choices: [][2]string{{"a", "A"}, {"b", "B"}}}}, Commands: []string{"x {m}"}}
	if c, _ := modCommands(ch, map[string]string{"m": "b; quit"}); len(c) != 1 || c[0] != "x a" {
		t.Fatalf("choice %v", c)
	}
}
