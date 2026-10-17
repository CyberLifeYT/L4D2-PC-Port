package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Setting describes one entry in l4d2_settings.ini and how the launcher's
// Settings page renders it. Names are the game's runtime settings (cvars).
type Setting struct {
	Key     string      `json:"key"`
	Section string      `json:"section"`
	Label   string      `json:"label"`
	Help    string      `json:"help"`
	Type    string      `json:"type"` // bool | choice | int | key | text
	Default string      `json:"default"`
	Choices [][2]string `json:"choices,omitempty"` // value, label
	Min     int         `json:"min,omitempty"`
	Max     int         `json:"max,omitempty"`
	Hidden  bool        `json:"hidden,omitempty"` // written to the ini, not shown
}

var SettingsSchema = []Setting{
	// Display
	{Key: "fullscreen", Section: "Display", Label: "Fullscreen", Type: "bool", Default: "true"},
	{Key: "vsync", Section: "Display", Label: "V-Sync", Help: "On holds the game to the display rate: when it cannot reach 60 it drops to an even 30. Off lets it run anywhere in between (around 40-50 in gameplay on a GTX 1080), with possible tearing.", Type: "bool", Default: "false"},
	{Key: "resolution_scale", Section: "Display", Label: "Internal resolution", Help: "Renders the game above its native 1280x720. Higher values cost GPU time.", Type: "choice", Default: "1",
		Choices: [][2]string{{"1", "1x  (1280x720, native)"}, {"2", "2x  (2560x1440)"}, {"3", "3x  (3840x2160)"}}},
	{Key: "anisotropic_override", Section: "Display", Label: "Texture filtering", Type: "choice", Default: "5",
		Choices: [][2]string{{"-1", "Game default"}, {"0", "Off"}, {"2", "2x anisotropic"}, {"3", "4x anisotropic"}, {"4", "8x anisotropic"}, {"5", "16x anisotropic"}}},
	{Key: "swap_post_effect", Section: "Display", Label: "Anti-aliasing (post)", Type: "choice", Default: "none",
		Choices: [][2]string{{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA (extreme)"}}},
	{Key: "video_mode_width", Section: "Display", Label: "Aspect ratio", Help: "The game renders natively at this shape - wider shows more of the world; HUD and menus keep their proportions instead of stretching. Movies stay 16:9 with side bars. Ultrawide modes use no more pixels than 16:9 (the Xbox 360's 10 MB of EDRAM must hold every frame), so combine them with Internal resolution 2x or 3x for a sharp picture.", Type: "choice", Default: "1280",
		Choices: [][2]string{{"1280", "16:9 (1280x720)"}, {"1152", "16:10 (1152x720)"}, {"1456", "21:9 ultrawide (1456x624)"}, {"1792", "32:9 super ultrawide (1792x504)"}, {"960", "4:3 (960x720)"}}},
	{Key: "l4d2_fps_max", Section: "Display", Label: "Frame-rate cap", Help: "The Xbox 360 version is capped at 30. Game simulation runs on a fixed tick, so higher caps only make motion smoother.", Type: "choice", Default: "60",
		Choices: [][2]string{{"30", "30 (console)"}, {"60", "60"}, {"120", "120"}, {"144", "144"}, {"0", "Unlimited"}}},
	{Key: "video_mode_height", Section: "Display", Type: "choice", Default: "720", Hidden: true},
	// Set while playing by the FOV Changer mod; reset to the game default at
	// each start so disabling the mod restores it.
	{Key: "l4d2_fov", Section: "Display", Type: "text", Default: "0", Hidden: true},
	{Key: "present_letterbox", Section: "Display", Label: "Keep aspect ratio (bars instead of stretching)", Type: "bool", Default: "true"},

	// Rendering
	{Key: "readback_resolve", Section: "Rendering", Label: "Text / render-to-texture accuracy", Help: "Full is required for correct menu and HUD text. Fast modes return stale data (scrambled letters) but stall the GPU less.", Type: "choice", Default: "full",
		Choices: [][2]string{{"full", "Full (correct text)"}, {"some", "Fast, on cache miss"}, {"fast", "Fastest (text may scramble)"}}},
	{Key: "d3d12_readback_resolve", Section: "Rendering", Label: "GPU-to-CPU readback", Type: "bool", Default: "true"},
	{Key: "occlusion_query_enable", Section: "Rendering", Label: "Real occlusion queries (HDR exposure)", Help: "Source's auto-exposure measures brightness with occlusion queries. Off = faked results = washed-out or dark scenes.", Type: "bool", Default: "true"},
	{Key: "direct_host_resolve", Section: "Rendering", Label: "Direct HDR resolve (faster, may band)", Help: "Leave off: the direct path skips the 360 HDR format conversion and causes colour banding.", Type: "bool", Default: "false"},
	{Key: "render_target_path_d3d12", Section: "Rendering", Label: "Render target emulation", Help: "Accurate (ROV) emulates the 360's EDRAM HDR formats exactly and fixes dark or banded lighting. Fast (RTV) costs less GPU time but lighting is wrong in dark areas.", Type: "choice", Default: "rov",
		Choices: [][2]string{{"rov", "Accurate (ROV)"}, {"", "Fast (RTV)"}}},
	{Key: "present_dither", Section: "Rendering", Label: "Output dithering", Help: "Hides banding in dark gradients (smoke, fog, flashlight falloff).", Type: "bool", Default: "true"},
	{Key: "gpu_allow_invalid_fetch_constants", Section: "Rendering", Label: "Console-accurate texture fetch", Help: "Keeps draws the console would still render. Needed for cutscene/movie playback; turn off only to debug.", Type: "bool", Default: "true"},
	{Key: "d3d12_readback_memexport", Section: "Rendering", Label: "Memexport readback", Help: "Lets the CPU see data shaders write to memory. Try it if particles or videos are missing; costs some FPS.", Type: "bool", Default: "false"},
	{Key: "async_shader_compilation", Section: "Rendering", Label: "Async shader compilation", Help: "Compiles new shaders in the background instead of freezing the game.", Type: "bool", Default: "true"},
	{Key: "store_shaders", Section: "Rendering", Label: "Shader cache", Help: "Keeps compiled shaders so later sessions don't stutter.", Type: "bool", Default: "true"},
	{Key: "d3d12_pipeline_creation_threads", Section: "Rendering", Label: "Shader compile threads", Type: "choice", Default: "-1",
		Choices: [][2]string{{"-1", "Auto"}, {"2", "2"}, {"4", "4"}, {"8", "8"}}},

	// Overlay
	{Key: "l4d2_overlay", Section: "Overlay", Label: "Show performance overlay at start (toggle in game: F2)", Type: "bool", Default: "false"},
	{Key: "l4d2_overlay_fps", Section: "Overlay", Label: "FPS, frame time, 1% low, stutters", Type: "bool", Default: "true"},
	{Key: "l4d2_overlay_graph", Section: "Overlay", Label: "Frame-time graph", Type: "bool", Default: "true"},
	{Key: "l4d2_overlay_render", Section: "Overlay", Label: "Resolution, draw calls, shader pipelines", Type: "bool", Default: "true"},
	{Key: "l4d2_overlay_memory", Section: "Overlay", Label: "Memory usage", Type: "bool", Default: "true"},
	{Key: "l4d2_overlay_settings", Section: "Overlay", Label: "Active GPU settings", Type: "bool", Default: "true"},
	{Key: "l4d2_shader_toast", Section: "Overlay", Label: "Shader compile notice", Help: "Small notice at the bottom of the screen while new shaders compile (the cause of first-time hitches).", Type: "bool", Default: "true"},
	{Key: "l4d2_overlay_corner", Section: "Overlay", Label: "Overlay position", Type: "choice", Default: "0",
		Choices: [][2]string{{"0", "Top left"}, {"1", "Top right"}, {"2", "Bottom left"}, {"3", "Bottom right"}}},

	// Input / audio
	{Key: "l4d2_pc_controls", Section: "Input & Audio", Label: "PC keyboard & mouse controls", Help: "Plays like the PC version: WASD to move, mouse to look, left click to fire, right click to shove, R reload, E use... Rebind every key on the Controls page. A controller keeps working alongside.", Type: "bool", Default: "true"},
	{Key: "l4d2_mouse_sensitivity", Section: "Input & Audio", Label: "Mouse sensitivity", Type: "choice", Default: "1.0",
		Choices: [][2]string{{"0.25", "0.25"}, {"0.5", "0.5"}, {"0.75", "0.75"}, {"1.0", "1.0"}, {"1.25", "1.25"}, {"1.5", "1.5"}, {"2.0", "2.0"}, {"3.0", "3.0"}, {"4.0", "4.0"}}},
	{Key: "l4d2_mouse_invert_y", Section: "Input & Audio", Label: "Invert mouse Y", Type: "bool", Default: "false"},
	{Key: "mnk_mode", Section: "Input & Audio", Label: "Controller-style keyboard (only when PC controls are off)", Help: "The emulator's generic keyboard-to-controller mapping. Not needed with PC controls.", Type: "bool", Default: "false"},
	{Key: "mnk_mouse", Section: "Input & Audio", Type: "bool", Default: "true", Hidden: true},
	{Key: "input_backend", Section: "Input & Audio", Label: "Controller API", Type: "choice", Default: "sdl",
		Choices: [][2]string{{"sdl", "SDL (all controllers)"}, {"xinput", "XInput (Xbox controllers)"}}},
	{Key: "audio_mute", Section: "Input & Audio", Label: "Mute audio", Type: "bool", Default: "false"},

	// Controls (Controls page). Comma-separated keys; must match the game's
	// l4d2_key_* defaults in port/src/pcinput.cpp.
	{Key: "l4d2_key_forward", Section: "Controls", Label: "Move forward", Type: "key", Default: "W"},
	{Key: "l4d2_key_back", Section: "Controls", Label: "Move back", Type: "key", Default: "S"},
	{Key: "l4d2_key_left", Section: "Controls", Label: "Move left", Type: "key", Default: "A"},
	{Key: "l4d2_key_right", Section: "Controls", Label: "Move right", Type: "key", Default: "D"},
	{Key: "l4d2_key_walk", Section: "Controls", Label: "Walk (hold)", Type: "key", Default: "Shift"},
	{Key: "l4d2_key_jump", Section: "Controls", Label: "Jump", Type: "key", Default: "Space"},
	{Key: "l4d2_key_crouch", Section: "Controls", Label: "Crouch (hold)", Type: "key", Default: "Ctrl"},
	{Key: "l4d2_key_fire", Section: "Controls", Label: "Fire / throw / use item", Type: "key", Default: "LMB"},
	{Key: "l4d2_key_shove", Section: "Controls", Label: "Shove / heal or give to teammate", Type: "key", Default: "RMB"},
	{Key: "l4d2_key_use", Section: "Controls", Label: "Use / revive / pick up", Type: "key", Default: "E"},
	{Key: "l4d2_key_reload", Section: "Controls", Label: "Reload", Type: "key", Default: "R"},
	{Key: "l4d2_key_zoom", Section: "Controls", Label: "Zoom (scoped weapons)", Type: "key", Default: "MMB"},
	{Key: "l4d2_key_flashlight", Section: "Controls", Label: "Flashlight", Type: "key", Default: "F"},
	{Key: "l4d2_key_switch_weapon", Section: "Controls", Label: "Switch weapon (primary / secondary)", Type: "key", Default: "Q,1,2,WheelUp,WheelDown"},
	{Key: "l4d2_key_grenade", Section: "Controls", Label: "Grenade", Type: "key", Default: "3"},
	{Key: "l4d2_key_health", Section: "Controls", Label: "First aid / defibrillator / upgrade", Type: "key", Default: "4"},
	{Key: "l4d2_key_pills", Section: "Controls", Label: "Pills / adrenaline", Type: "key", Default: "5"},
	{Key: "l4d2_key_voice", Section: "Controls", Label: "Voice commands", Type: "key", Default: "Z,C"},
	{Key: "l4d2_key_turn_around", Section: "Controls", Label: "Quick turn (180)", Type: "key", Default: "V"},
	{Key: "l4d2_key_scoreboard", Section: "Controls", Label: "Scoreboard / vote no", Type: "key", Default: "Tab"},
	{Key: "l4d2_key_pause", Section: "Controls", Label: "Pause menu / vote yes", Type: "key", Default: "Escape"},
	{Key: "l4d2_key_menu_select", Section: "Controls", Label: "Menu: select", Type: "key", Default: "Enter"},
	{Key: "l4d2_key_menu_back", Section: "Controls", Label: "Menu: back", Type: "key", Default: "Backspace"},
	{Key: "l4d2_key_menu_up", Section: "Controls", Label: "Menu: up", Type: "key", Default: "Up"},
	{Key: "l4d2_key_menu_down", Section: "Controls", Label: "Menu: down", Type: "key", Default: "Down"},
	{Key: "l4d2_key_menu_left", Section: "Controls", Label: "Menu: left", Type: "key", Default: "Left"},
	{Key: "l4d2_key_menu_right", Section: "Controls", Label: "Menu: right", Type: "key", Default: "Right"},

	// Written by the launcher from enabled mods (Mods page).
	{Key: ModCommandsKey, Section: "Mods", Type: "text", Default: "", Hidden: true},
	{Key: ModLaunchKey, Section: "Mods", Type: "text", Default: "", Hidden: true},

	// Debug
	{Key: "l4d2_perf_log", Section: "Debug", Label: "Performance log", Help: "Writes logs\\perf_summary.csv every 5 s: game FPS, lowest FPS, stutters, shaders compiled, memory.", Type: "bool", Default: "true"},
	{Key: "l4d2_autoshot", Section: "Debug", Label: "Automatic screenshots", Help: "Saves a screenshot to logs\\shots every few seconds - useful for bug reports. F12 always takes one.", Type: "choice", Default: "0",
		Choices: [][2]string{{"0", "Off"}, {"3", "Every 3 s"}, {"10", "Every 10 s"}, {"30", "Every 30 s"}}},
	{Key: "log_level", Section: "Debug", Label: "Log level", Help: "Debug logging costs frame time; keep at Info for play.", Type: "choice", Default: "info",
		Choices: [][2]string{{"warn", "Warnings only"}, {"info", "Info"}, {"debug", "Debug (slow)"}}},
}

func settingsPath(installDir string) string { return filepath.Join(installDir, SettingsFileName) }

// ReadSettings returns schema defaults overlaid with values in the ini.
func ReadSettings(installDir string) map[string]string {
	vals := map[string]string{}
	for _, s := range SettingsSchema {
		vals[s.Key] = s.Default
	}
	readIni(filepath.Join(installDir, DefaultSettingsRel), vals)
	readIni(settingsPath(installDir), vals)
	return vals
}

func readIni(p string, into map[string]string) {
	f, err := os.Open(p)
	if err != nil {
		return
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if i := strings.Index(line, "#"); i >= 0 {
			line = line[:i]
		}
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "[") || strings.HasPrefix(line, ";") {
			continue
		}
		if k, v, ok := strings.Cut(line, "="); ok {
			into[strings.TrimSpace(k)] = strings.TrimSpace(v)
		}
	}
}

// WriteSettings writes every schema key (plus any extra keys already in the
// file, e.g. hand-added runtime settings) grouped by section.
// aspectHeight is the native height for each aspect-ratio width: ultrawide
// modes keep the pixel count within 16:9's (1280x720) so frames always fit
// the console's EDRAM. 1680 is the old 21:9 value from launcher 1.1-1.2.
func aspectHeight(width string) (string, string) {
	switch width {
	case "1456", "1680":
		return "1456", "624"
	case "1792", "2560":
		return "1792", "504"
	case "1152", "960", "1280":
		return width, "720"
	}
	return "1280", "720"
}

func WriteSettings(installDir string, vals map[string]string) error {
	if w, ok := vals["video_mode_width"]; ok {
		vals["video_mode_width"], vals["video_mode_height"] = aspectHeight(w)
	}
	existing := map[string]string{}
	readIni(settingsPath(installDir), existing)
	known := map[string]bool{}
	var b strings.Builder
	b.WriteString("# L4D2 PC Port settings - written by the launcher. Safe to edit by hand;\n")
	b.WriteString("# any runtime setting name=value is accepted. Applied at game start;\n")
	b.WriteString("# see logs\\settings_applied.txt for what took effect.\n")
	section := ""
	for _, s := range SettingsSchema {
		known[s.Key] = true
		if s.Section != section {
			section = s.Section
			fmt.Fprintf(&b, "\n[%s]\n", section)
		}
		v, ok := vals[s.Key]
		if !ok {
			v = s.Default
		}
		fmt.Fprintf(&b, "%s=%s\n", s.Key, v)
	}
	extra := false
	for k, v := range existing {
		if known[k] {
			continue
		}
		if !extra {
			b.WriteString("\n[Custom]\n")
			extra = true
		}
		if nv, ok := vals[k]; ok {
			v = nv
		}
		fmt.Fprintf(&b, "%s=%s\n", k, v)
	}
	if err := os.MkdirAll(installDir, 0o755); err != nil {
		return err
	}
	return os.WriteFile(settingsPath(installDir), []byte(b.String()), 0o644)
}

// MergeSettings is called after a patch: keeps user values, adds new keys.
func MergeSettings(installDir string) error {
	return WriteSettings(installDir, ReadSettings(installDir))
}
