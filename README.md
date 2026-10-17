# Left 4 Dead 2 — PC Port (Xbox 360 recompilation)

An unofficial native Windows port of the **Xbox 360** release of Left 4 Dead 2. The game's PowerPC code is statically recompiled to x86-64 with the ReXGlue toolchain and runs on a Direct3D 12 renderer — no emulator, no console.

> **You need your own copy of the game.** This repository contains no game data, no Valve code and no recompiled binaries. The launcher installs the game from **your** Xbox 360 disc image (`.iso`).

## Install (players)

1. Download `l4d2-launcher.exe` from the latest [release](../../releases/latest).
2. Run it and open **Install**.
3. **Game disc** — pick your Left 4 Dead 2 Xbox 360 `.iso` (a 1:1 disc dump). The launcher checks that it is a readable Xbox 360 disc and that its title ID is Left 4 Dead 2 (`454108D4`) before anything is copied. An already-extracted folder also works.
4. **Install location** — choose where the game goes (about 8 GB free).
5. **Port patch** — *Latest from GitHub* downloads the newest `l4d2-port-patch-*.zip` from this repo's releases. Pick the **update channel** under it: **Release** (stable builds only) or **Pre-release** (newest test builds, may have bugs). You can also pick a patch zip you downloaded yourself.
6. Press **Begin install**, then **Launch** on the Play page.

Requirements: Windows 10/11 x64, a Direct3D 12 GPU, the [Microsoft Edge WebView2 runtime](https://developer.microsoft.com/microsoft-edge/webview2/) (preinstalled on Windows 11) and the [Visual C++ 2015-2022 x64 runtime](https://aka.ms/vs/17/release/vc_redist.x64.exe) (the launcher offers it if missing).

### In game

| Key | |
|---|---|
| **F12** | Screenshot (saved to `logs\shots\` in the install folder) |
| **F2** | Performance overlay: FPS, frame time, 1% low, stutter count, frame-time graph, output/internal resolution and scale, draw calls, shader pipelines compiled (and whether compiling right now), render targets, cache hit rates, GPU, memory, active render settings |
| **F4** | Live settings panel |
| **F3** / **`** | Runtime debug overlay / console |

While new shaders are being compiled a small **"Compiling shaders"** notice appears at the bottom of the screen (with the count), and at start-up it shows how many shaders were loaded from the cache. The game also writes `logs\perf_summary.csv` every 5 s (game FPS, lowest FPS, stutters, shaders compiled, memory) so performance problems can be reported with numbers.

### PC controls

The port has native keyboard & mouse controls with the PC layout, on by default (**Settings › Input & Audio › PC keyboard & mouse controls**). An Xbox controller works alongside at any time. The mouse is captured while the game window has focus.

| Action | Default |
|---|---|
| Move / walk | **W A S D** / hold **Shift** |
| Look / aim | **Mouse** |
| Fire / throw / use item | **Left click** |
| Shove / heal or give to a teammate | **Right click** |
| Jump / crouch | **Space** / hold **Ctrl** |
| Use / revive / pick up | **E** |
| Reload | **R** |
| Flashlight | **F** |
| Switch weapon (primary / secondary) | **Q**, **1**, **2** or **mouse wheel** |
| Grenade / first aid / pills | **3** / **4** / **5** |
| Zoom (scoped weapons) | **Middle click** |
| Voice commands / quick turn | **Z** or **C** / **V** |
| Scoreboard / pause menu | **Tab** / **Esc** |
| Menus: move / select / back | **Arrow keys** / **Enter** / **Backspace** |

**Rebinding:** use the launcher's **Controls** page. Click **+** next to an action and press a key or mouse button, or roll the wheel; click a key to remove it. Bindings are stored as `l4d2_key_<action>=Key1,Key2` in `l4d2_settings.ini`.

**How it works:** the 360 game only reads an Xbox pad, so the port turns PC input into pad 0's state right where the game reads it. Each key and mouse button presses the pad control for its action (for example left click pulls the right trigger, which is Fire). Mouse motion comes from the game window's relative-mouse events, which SDL reads from Windows raw input. Every time the game polls the pad, the motion since the last poll becomes a right-stick value that is linear in mouse speed and starts just past the stick deadzone. The same hand movement therefore turns the view by the same amount at any frame rate. The game's own look curve is linear at its defaults, and aim-assist damping is off, so nothing adds acceleration on top.

**Tuning:** sensitivity and invert-Y are in Settings. Two values in `l4d2_settings.ini` fine-tune the feel:
- `l4d2_mouse_counts_full` sets how many mouse counts per second equal a full stick push.
- `l4d2_mouse_deadzone` sets where the stick output starts.

Bug reports can include `logs\pcinput.txt`, which logs the input event counts every 5 s.

### Settings

The launcher's **Settings** page edits `l4d2_settings.ini` in the install folder; changes apply on the next launch. Highlights:

- **Text accuracy** (`readback_resolve`): *Full* is needed for correct menu/HUD text — Source draws glyphs on the GPU and reads them back. Faster modes scramble letters.
- **Render target emulation** (`render_target_path_d3d12`): *Accurate (ROV)* — the default — reproduces the 360's EDRAM formats exactly. It is what makes in-game lighting correct (no posterised/banded colours) and makes the Bink intro movies show picture instead of a black screen. *Fast (RTV)* is cheaper but lighting and movies are wrong.
- **Real occlusion queries**: Source's HDR auto-exposure depends on them.
- Overlay sections, resolution scale (1x–3x), anisotropic filtering, FXAA, V-Sync, PC controls and mouse sensitivity, log level.

### PC features

- **PC keyboard & mouse** (Settings › Input, Controls page): PC layout with mouse look, all keys rebindable. See [PC controls](#pc-controls).
- **Frame-rate cap** (Settings › Display): the 360 build caps itself with the engine's `fps_max` ConVar (default 30, hidden from the console). The port writes the chosen value — 30, 60, 120, 144 or unlimited — into that ConVar every frame. The default is 60 with V-Sync off: menus run at 60 and gameplay currently runs at about 40–50 FPS on a GTX 1080 (it was held to 30 before). With V-Sync on, frames that miss 60 are held to an even 30. Game simulation runs on Source's fixed server tick and is interpolated for rendering, so physics, AI and particles keep their timing at any frame rate.
- **Aspect ratio** (Settings › Display): the game is given a native 16:9, 16:10, 21:9 (1680x720) or 4:3 video mode instead of being stretched. Source lays the HUD and menus out for the actual shape, and movies stay 16:9 with side bars.
- **Mods** (launcher › Mods): loose-file mods load ahead of the game's own files.
  - Each mod is a folder in `game\mods\<name>\` laid out like `left4dead2\`, or containing a `zip0.360.zip`.
  - When you press Launch, the launcher puts enabled mods, in your chosen order, at the top of `gameinfo.xtx`'s search paths. The original is kept as `gameinfo.xtx.orig`.
  - A `mod.json` can add sliders, toggles and choices on the Mods page, plus game console commands that use their values. The port runs those through the game's own command buffer and unhides development-only ConVars first.
  - **Add starter mods** installs **FOV Changer** (a field-of-view slider; also the template for settings mods) and **Skip Intro** (no start-up movies, and START is pressed for you on the title screen, so the game opens on the main menu).
  - PC `.vpk` addons are unpacked to loose files when added. Scripts, configs, text and 360-built maps work; PC-format models, textures and sounds need their 360 versions.
  - Full guide: [docs/MODDING.md](docs/MODDING.md).

### Updates

**Updates → Check for updates** compares your installed patch with the newest build on your chosen channel (**Release** or **Pre-release**, switchable on the Updates page). Installing a patch verifies every file's SHA-256 before anything is replaced, backs up the files it replaces to `.backup\<old version>`, and keeps your settings.

## Repository layout

| Path | What |
|---|---|
| `launcher/` | The launcher (Go + WebView2). XDVDFS/ISO reader, XEX title check, installer, patcher, settings UI. |
| `port/` | The port project: runtime hooks (`src/`), import/stub glue (`src/stubs/`), recompiler manifest (`l4d2_manifest.toml`), build config. |
| `docs/` | Patch format and release process. |

## Building

### Launcher

```
cd launcher
go build -trimpath -ldflags "-H windowsgui -s -w" -o l4d2-launcher.exe .
```
Cross-compiling from Linux/macOS works with `GOOS=windows GOARCH=amd64`. Command-line modes:

| | |
|---|---|
| `--inspect-iso <iso> [report]` | Check a disc image |
| `--install <iso\|folder> <dir> <patch.zip\|github\|github-pre\|skip> [log]` | Headless install |
| `--make-patch <buildDir> <version> <out.zip> [notes]` | Build a patch zip |
| `--publish <zip> <version> [--prerelease] [--notes text] [--file extra] [--repo owner/name] [--log file]` | Upload a patch to a GitHub Release |
 `go test` runs the ISO, install and patch tests against synthetic disc images.

### Port

Requires the ReXGlue SDK 0.10.0, Visual Studio 2022+ with C++ and Clang tools, and the game files extracted next to the project as `assets/` (the launcher's install, or `l4d2-launcher --inspect-iso` to check your ISO first).

```
port\scripts\build.bat full     :: codegen + vscript table fix + build
port\scripts\build.bat          :: incremental
port\scripts\run_dev.bat        :: run against ..\assets with a log in port\logs
port\scripts\test_map.bat       :: windowed 3-minute run straight into Dead Center; screenshots +
                                :: perf_summary.csv land in port\testruns\
port\scripts\perf_matrix.bat    :: the same run with several settings files, for A/B comparisons
port\scripts\feature_tests.bat  :: frame-rate / 21:9 runs; an autopilot presses START/A to reach a campaign
port\scripts\pc_tests.bat       :: PC controls: real OS mouse moves, W, clicks, R, F (SendInput, only while
                                :: the game window is in front) + console commands; logs\pcinput.txt
port\scripts\cal_tests.bat      :: stick-deadzone calibration (fixed right-stick levels, autoshots every second)
port\scripts\v_tests.bat        :: Skip Intro (-novid + auto START), console-started map, l4d2_fov changes, input test
```

### Making a patch release

| Script | What it does |
|---|---|
| `port\scripts\make_patch.bat 1.0.1 "notes"` | Builds `port\dist\l4d2-port-patch-1.0.1.zip` only — upload it yourself |
| `port\scripts\release_patch.bat 1.0.1 "notes"` | Builds the zip **and** publishes it as a full GitHub Release `v1.0.1` (with the launcher exe attached) |
| `port\scripts\pre_release_patch.bat 1.1.0-beta.1 "notes"` | Same, but as a **pre-release** — only launchers on the *Pre-release* channel offer it |

Publishing uses your GitHub login: `GITHUB_TOKEN`/`GH_TOKEN` if set, otherwise the GitHub CLI (`gh auth login`), otherwise Git's credential manager (the login `git push` uses). Re-running for an existing version replaces its files. Versions compare like semver: `1.1.0-beta.2` < `1.1.0`. See [docs/PATCHES.md](docs/PATCHES.md).

## Status

Boots, intro movies, menus, text and gameplay work, with correct lighting on the accurate (ROV) render path. Gameplay runs at roughly 40–50 FPS uncapped (60 in menus) on a GTX 1080; short dips happen while new shaders compile (the shader cache removes them on later runs). Known issues are tracked in [Issues](../../issues).

## Legal

Left 4 Dead, Left 4 Dead 2 and Valve are trademarks of Valve Corporation; Xbox 360 is a trademark of Microsoft. This project is not affiliated with or endorsed by either. No game content is distributed here — you must own the game.

No license has been chosen for this repository yet.
