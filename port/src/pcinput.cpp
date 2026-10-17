// L4D2 PC port - native keyboard & mouse controls.
//
// The Xbox 360 game only understands an Xbox 360 pad, so PC controls are
// delivered as pad 0's state - but with a PC layout and PC behaviour:
//
//  * Keys and mouse buttons come from the game window's own input events
//    (the same events the SDK's windowing layer delivers), mapped through
//    rebindable actions (l4d2_key_* settings) whose defaults match Left 4
//    Dead 2 on PC: WASD, mouse look, LMB fire, RMB shove, R reload, E use...
//  * Mouse motion is the window's relative motion (SDL relative mode, which
//    reads Windows raw input). Every time the game polls the pad, the motion
//    since the previous poll is turned into a right-stick value that is linear
//    in mouse speed and starts just past the stick deadzone, so the same hand
//    movement turns the view by the same amount at any frame rate.
//
// The previous version registered its own WM_INPUT listener. Windows allows
// one raw-input mouse registration per process and SDL makes its own when it
// captures the cursor, so that listener stopped receiving anything - the view
// could not turn. Reading the window events avoids the conflict.
//
// src/stubs/hook_inputsystem.cpp calls L4d2PcEditPadState() after the game's
// XInputGetState for pad 0. A real controller keeps working: its sticks and
// buttons pass through and PC input is merged on top.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

REXCVAR_DEFINE_BOOL(l4d2_pc_controls, true, "L4D2 Input",
                    "Native PC keyboard & mouse controls (PC layout, mouse look, rebindable l4d2_key_* actions)");
REXCVAR_DEFINE_BOOL(l4d2_raw_mouse, false, "L4D2 Input", "Old name for l4d2_pc_controls (true turns PC controls on)");
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_sensitivity, 1.0, "L4D2 Input", "Mouse look sensitivity multiplier");
REXCVAR_DEFINE_BOOL(l4d2_mouse_invert_y, false, "L4D2 Input", "Invert mouse vertical look");
// Measured in game (scripts\cal_tests.bat): right-stick levels up to 0.25 do
// not turn the view, above it the turn rate is linear and extrapolates to
// zero at 0.265 - the standard XInput right-thumb deadzone (8689/32767),
// rescaled to the full range by the game.
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_deadzone, 0.265, "L4D2 Input",
                      "Stick deadzone the game applies (0-0.5); mouse look output starts just past it");
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_turn_boost, 3.0, "L4D2 Input",
                      "While the mouse is in use the game's look speed is multiplied by this (and the mouse "
                      "mapping divided by it), so fast flicks are not capped by the stick's top turn speed");
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_counts_full, 2400.0, "L4D2 Input",
                      "Mouse counts per second that equal a full stick deflection at sensitivity 1");
REXCVAR_DEFINE_DOUBLE(l4d2_walk_speed, 0.45, "L4D2 Input", "Stick deflection used while the walk key is held");
REXCVAR_DEFINE_BOOL(l4d2_test_input, false, "L4D2 Debug",
                    "Automated tests: after 150 s send real mouse moves, key presses and clicks with SendInput");
REXCVAR_DEFINE_BOOL(l4d2_test_calibrate, false, "L4D2 Debug",
                    "Automated tests: after 140 s hold fixed right-stick levels to measure the game's look deadzone");
REXCVAR_DEFINE_BOOL(l4d2_skip_press_start, false, "L4D2 Input",
                    "Press START for you on the PRESS START title screen (Skip Intro mod), so the game "
                    "signs in your profile and storage device as usual and opens on the main menu");
REXCVAR_DEFINE_BOOL(l4d2_test_autopress, false, "L4D2 Debug",
                    "Automated tests: press START/A on a schedule to reach gameplay");

// Rebindable actions. Value: comma-separated key names (A-Z, 0-9, F1-F12,
// Space, Enter, Escape, Tab, Backspace, Shift, Ctrl, Alt, Up/Down/Left/Right,
// LMB, RMB, MMB, Mouse4, Mouse5, WheelUp, WheelDown). Empty = unbound.
#define L4D2_KEY(name, def, desc) REXCVAR_DEFINE_STRING(l4d2_key_##name, def, "L4D2 Controls", desc)
L4D2_KEY(forward, "W", "Move forward");
L4D2_KEY(back, "S", "Move back");
L4D2_KEY(left, "A", "Move left");
L4D2_KEY(right, "D", "Move right");
L4D2_KEY(walk, "Shift", "Walk (hold)");
L4D2_KEY(jump, "Space", "Jump");
L4D2_KEY(crouch, "Ctrl", "Crouch (hold)");
L4D2_KEY(fire, "LMB", "Fire / throw / use item");
L4D2_KEY(shove, "RMB", "Shove / heal teammate / give item");
L4D2_KEY(use, "E", "Use / revive / pick up");
L4D2_KEY(reload, "R", "Reload");
L4D2_KEY(zoom, "MMB", "Zoom (scoped weapons)");
L4D2_KEY(flashlight, "F", "Flashlight");
L4D2_KEY(switch_weapon, "Q,1,2,WheelUp,WheelDown", "Switch primary / secondary weapon");
L4D2_KEY(grenade, "3", "Select grenade");
L4D2_KEY(health, "4", "Select first aid / defib / upgrade pack");
L4D2_KEY(pills, "5", "Select pills / adrenaline");
L4D2_KEY(voice, "Z,C", "Voice commands");
L4D2_KEY(turn_around, "V", "Quick turn (180 degrees)");
L4D2_KEY(scoreboard, "Tab", "Scoreboard / vote no");
L4D2_KEY(pause, "Escape", "Pause menu / vote yes");
L4D2_KEY(menu_select, "Enter", "Menu: select");
L4D2_KEY(menu_back, "Backspace", "Menu: back");
L4D2_KEY(menu_up, "Up", "Menu: up");
L4D2_KEY(menu_down, "Down", "Menu: down");
L4D2_KEY(menu_left, "Left", "Menu: left");
L4D2_KEY(menu_right, "Right", "Menu: right");

namespace l4d2 {
double AppliedLookBoost();  // console.cpp
void NoteMouseLook();       // console.cpp
}  // namespace l4d2

namespace {

using rex::ui::VirtualKey;

// XINPUT_GAMEPAD_* bits.
enum : uint16_t {
  kDUp = 0x0001, kDDown = 0x0002, kDLeft = 0x0004, kDRight = 0x0008,
  kStart = 0x0010, kBack = 0x0020, kLThumb = 0x0040, kRThumb = 0x0080,
  kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000,
};
enum class Target { kButton, kLT, kRT, kFwd, kBackMove, kLeftMove, kRightMove, kWalk };

struct Action {
  const char* name;
  const std::string& (*value)();
  Target target;
  uint16_t button;
};

#define L4D2_ACT(n, t, b) {#n, [] () -> const std::string& { static thread_local std::string s; s = REXCVAR_GET(l4d2_key_##n); return s; }, t, b}
const Action kActions[] = {
    L4D2_ACT(forward, Target::kFwd, 0),       L4D2_ACT(back, Target::kBackMove, 0),
    L4D2_ACT(left, Target::kLeftMove, 0),     L4D2_ACT(right, Target::kRightMove, 0),
    L4D2_ACT(walk, Target::kWalk, 0),         L4D2_ACT(jump, Target::kButton, kA),
    L4D2_ACT(crouch, Target::kButton, kLB),   L4D2_ACT(fire, Target::kRT, 0),
    L4D2_ACT(shove, Target::kLT, 0),          L4D2_ACT(use, Target::kButton, kX),
    L4D2_ACT(reload, Target::kButton, kB),    L4D2_ACT(zoom, Target::kButton, kRThumb),
    L4D2_ACT(flashlight, Target::kButton, kDUp), L4D2_ACT(switch_weapon, Target::kButton, kY),
    L4D2_ACT(grenade, Target::kButton, kDLeft), L4D2_ACT(health, Target::kButton, kDRight),
    L4D2_ACT(pills, Target::kButton, kDDown), L4D2_ACT(voice, Target::kButton, kLThumb),
    L4D2_ACT(turn_around, Target::kButton, kRB), L4D2_ACT(scoreboard, Target::kButton, kBack),
    L4D2_ACT(pause, Target::kButton, kStart), L4D2_ACT(menu_select, Target::kButton, kA),
    L4D2_ACT(menu_back, Target::kButton, kB), L4D2_ACT(menu_up, Target::kButton, kDUp),
    L4D2_ACT(menu_down, Target::kButton, kDDown), L4D2_ACT(menu_left, Target::kButton, kDLeft),
    L4D2_ACT(menu_right, Target::kButton, kDRight),
};
constexpr size_t kNumActions = sizeof(kActions) / sizeof(kActions[0]);

// Input codes: 0x01-0xFE Windows virtual keys (mouse buttons use their VK
// codes 0x01/0x02/0x04/0x05/0x06), plus two wheel pseudo-keys.
constexpr int kWheelUp = 0x100, kWheelDown = 0x101, kNumCodes = 0x102;

std::string Lower(std::string s) {
  for (auto& c : s) c = char(tolower((unsigned char)c));
  return s;
}

int ParseKey(std::string name) {
  size_t a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
  if (a == std::string::npos) return 0;
  name = name.substr(a, b - a + 1);
  const std::string l = Lower(name);
  static const struct { const char* n; int code; } kNames[] = {
      {"lmb", 0x01}, {"mouse1", 0x01}, {"rmb", 0x02}, {"mouse2", 0x02}, {"mmb", 0x04}, {"mouse3", 0x04},
      {"mouse4", 0x05}, {"mouse5", 0x06}, {"wheelup", kWheelUp}, {"mwheelup", kWheelUp},
      {"wheeldown", kWheelDown}, {"mwheeldown", kWheelDown}, {"space", 0x20}, {"enter", 0x0D},
      {"return", 0x0D}, {"escape", 0x1B}, {"esc", 0x1B}, {"tab", 0x09}, {"backspace", 0x08},
      {"shift", 0x10}, {"ctrl", 0x11}, {"control", 0x11}, {"alt", 0x12}, {"up", 0x26},
      {"down", 0x28}, {"left", 0x25}, {"right", 0x27}, {"capslock", 0x14}, {"caps", 0x14},
      {"insert", 0x2D}, {"delete", 0x2E}, {"home", 0x24}, {"end", 0x23}, {"pageup", 0x21},
      {"pagedown", 0x22}, {"backtick", 0xC0},
  };
  for (const auto& k : kNames)
    if (l == k.n) return k.code;
  if (name.size() == 1) {
    const char c = char(toupper((unsigned char)name[0]));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
  }
  if (l.size() >= 2 && l[0] == 'f' && isdigit((unsigned char)l[1])) {
    const int n = atoi(l.c_str() + 1);
    if (n >= 1 && n <= 24) return 0x70 + n - 1;
  }
  const int vk = int(rex::ui::ParseVirtualKey(name));
  return (vk > 0 && vk < 0xFF) ? vk : 0;
}

// Collapse left/right modifier keys onto the generic codes the binds use.
int Canonical(int vk) {
  switch (vk) {
    case 0xA0: case 0xA1: return 0x10;
    case 0xA2: case 0xA3: return 0x11;
    case 0xA4: case 0xA5: return 0x12;
    default: return vk;
  }
}

class PcInput final : public rex::ui::WindowInputListener {
 public:
  bool Enabled() const { return enabled_; }
  // The player has pressed a key or mouse button in the game window.
  bool UserActive() const { return key_events_.load() + button_events_.load() > 0; }
  void SetEnabled(bool v) { enabled_ = v; }

  void OnKeyDown(rex::ui::KeyEvent& e) override { Key(e, true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { Key(e, false); }
  void OnMouseDown(rex::ui::MouseEvent& e) override { Button(e, true); }
  void OnMouseUp(rex::ui::MouseEvent& e) override { Button(e, false); }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    dx_ += e.dx();
    dy_ += e.dy();
    ++move_events_;
  }
  void OnMouseWheel(rex::ui::MouseEvent& e) override {
    if (!enabled_ || e.scroll_y() == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    // A wheel notch holds its button for ~90 ms so the game sees a press.
    wheel_until_[e.scroll_y() > 0 ? 0 : 1] = Now() + 0.09;
  }

  // Test hook: same path as real motion.
  void InjectMotion(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex_);
    dx_ += dx;
    dy_ += dy;
  }

  void ReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fill(std::begin(down_), std::end(down_), false);
    dx_ = dy_ = 0;
  }

  // Edits pad 0's X_INPUT_STATE gamepad in place (big-endian guest layout).
  bool Edit(uint8_t* p, bool focused) {
    if (!enabled_) return false;
    RefreshBinds();
    bool down[kNumCodes];
    float dx, dy;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!focused) {
        std::fill(std::begin(down_), std::end(down_), false);
        dx_ = dy_ = 0;
      }
      memcpy(down, down_, sizeof(down));
      const double now = Now();
      down[kWheelUp] = now < wheel_until_[0];
      down[kWheelDown] = now < wheel_until_[1];
      dx = dx_; dy = dy_;
      dx_ = dy_ = 0;
    }
    uint16_t buttons = 0;
    bool lt = false, rt = false, fwd = false, bwd = false, lft = false, rgt = false, walk = false;
    std::unique_lock<std::mutex> binds_lock(binds_mutex_);
    for (size_t i = 0; i < kNumActions; ++i) {
      bool on = false;
      for (int code : binds_[i]) on = on || (code > 0 && code < kNumCodes && down[code]);
      if (!on) continue;
      switch (kActions[i].target) {
        case Target::kButton: buttons |= kActions[i].button; break;
        case Target::kLT: lt = true; break;
        case Target::kRT: rt = true; break;
        case Target::kFwd: fwd = true; break;
        case Target::kBackMove: bwd = true; break;
        case Target::kLeftMove: lft = true; break;
        case Target::kRightMove: rgt = true; break;
        case Target::kWalk: walk = true; break;
      }
    }
    binds_lock.unlock();
    bool changed = false;
    if (buttons) {
      p[4] |= uint8_t(buttons >> 8);
      p[5] |= uint8_t(buttons);
      changed = true;
    }
    if (lt) { p[6] = 255; changed = true; }
    if (rt) { p[7] = 255; changed = true; }
    const int mx = int(rgt) - int(lft), my = int(fwd) - int(bwd);
    if (mx || my) {
      double s = walk ? std::clamp(REXCVAR_GET(l4d2_walk_speed), 0.1, 1.0) : 1.0;
      if (mx && my) s *= 0.70710678;
      Put16(p + 8, int16_t(mx * s * 32767.0));
      Put16(p + 10, int16_t(my * s * 32767.0));
      changed = true;
    }
    int16_t rx, ry;
    if (Look(dx, dy, &rx, &ry)) {
      Put16(p + 12, rx);
      Put16(p + 14, ry);
      changed = true;
    }
    if (changed) {
      ++edits_;
      last_buttons_ = buttons;
    }
    return changed;
  }

  void Stats(char* buf, size_t n) {
    snprintf(buf, n, "move_events=%llu key_events=%llu button_events=%llu pad_edits=%llu look_polls=%llu last_buttons=0x%04X",
             (unsigned long long)move_events_.load(), (unsigned long long)key_events_.load(),
             (unsigned long long)button_events_.load(), (unsigned long long)edits_.load(),
             (unsigned long long)look_polls_.load(), last_buttons_.load());
  }

 private:
  static double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
  }
  static void Put16(uint8_t* p, int16_t v) {
    p[0] = uint8_t(uint16_t(v) >> 8);
    p[1] = uint8_t(uint16_t(v));
  }

  bool IsBound(int code) {
    std::lock_guard<std::mutex> lock(binds_mutex_);
    for (const auto& b : binds_)
      for (int c : b)
        if (c == code) return true;
    return false;
  }

  void Key(rex::ui::KeyEvent& e, bool down) {
    if (!enabled_) return;
    RefreshBinds();
    const int vk = Canonical(int(e.virtual_key()));
    if (vk <= 0 || vk >= 0xFF) return;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      down_[vk] = down;
      down_[int(e.virtual_key()) & 0xFF] = down;
    }
    ++key_events_;
    // Keep bound keys away from the SDK's controller-style key mapping (it
    // would press a second, different pad button for the same key). Unbound
    // keys - F2/F3/F4/F12, the console key - continue to their handlers.
    if (IsBound(vk)) e.set_handled(true);
  }

  void Button(rex::ui::MouseEvent& e, bool down) {
    if (!enabled_) return;
    int code = 0;
    switch (e.button()) {
      case rex::ui::MouseEvent::Button::kLeft: code = 0x01; break;
      case rex::ui::MouseEvent::Button::kRight: code = 0x02; break;
      case rex::ui::MouseEvent::Button::kMiddle: code = 0x04; break;
      case rex::ui::MouseEvent::Button::kX1: code = 0x05; break;
      case rex::ui::MouseEvent::Button::kX2: code = 0x06; break;
      default: return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    down_[code] = down;
    ++button_events_;
  }

  void RefreshBinds() {
    const double now = Now();
    if (now < next_bind_refresh_) return;
    next_bind_refresh_ = now + 1.0;
    std::lock_guard<std::mutex> lock(binds_mutex_);
    for (size_t i = 0; i < kNumActions; ++i) {
      binds_[i].clear();
      std::string v = kActions[i].value();
      size_t pos = 0;
      while (pos <= v.size()) {
        size_t c = v.find(',', pos);
        if (c == std::string::npos) c = v.size();
        if (int code = ParseKey(v.substr(pos, c - pos))) binds_[i].push_back(code);
        pos = c + 1;
      }
    }
  }

  // Mouse motion since the last poll -> right stick. Returns false while the
  // mouse is still so a controller's right stick passes through.
  bool Look(float dx, float dy, int16_t* out_rx, int16_t* out_ry) {
    ++look_polls_;
    const double now = Now();
    double dt = last_poll_ > 0 ? now - last_poll_ : 1.0 / 30.0;
    last_poll_ = now;
    dt = std::clamp(dt, 0.002, 0.1);  // several polls per frame share one interval
    if (dx != 0 || dy != 0) last_motion_ = now;
    if (dx == 0 && dy == 0 && (last_motion_ == 0 || now - last_motion_ > 0.25)) return false;
    // The game's look speed is boosted while the mouse is active (console.cpp);
    // scale the mapping by the boost that is actually applied right now.
    const double full = std::max(1.0, REXCVAR_GET(l4d2_mouse_counts_full)) * l4d2::AppliedLookBoost() /
                        std::max(0.01, REXCVAR_GET(l4d2_mouse_sensitivity));
    if (dx != 0 || dy != 0) l4d2::NoteMouseLook();
    const double dz = std::clamp(REXCVAR_GET(l4d2_mouse_deadzone), 0.0, 0.5);
    auto shape = [&](double counts) -> int16_t {
      const double v = counts / dt / full;  // linear in mouse speed
      if (std::fabs(v) < 1e-4) return 0;
      const double mag = dz + (1.0 - dz) * std::min(1.0, std::fabs(v));  // skip the deadzone
      return int16_t(std::copysign(mag, v) * 32767.0);
    };
    *out_rx = shape(dx);
    *out_ry = shape(REXCVAR_GET(l4d2_mouse_invert_y) ? dy : -dy);
    return true;
  }

  std::atomic<bool> enabled_{false};
  std::mutex mutex_;
  bool down_[kNumCodes] = {};
  float dx_ = 0, dy_ = 0;
  double wheel_until_[2] = {};
  std::mutex binds_mutex_;
  std::vector<int> binds_[kNumActions];
  double next_bind_refresh_ = 0;
  double last_poll_ = 0, last_motion_ = 0;
  std::atomic<uint64_t> move_events_{0}, key_events_{0}, button_events_{0}, edits_{0}, look_polls_{0};
  std::atomic<uint16_t> last_buttons_{0};
};

PcInput g_input;
std::atomic<int> g_cal_rx{INT32_MIN};  // calibration: forced right-stick X
rex::ui::Window* g_window = nullptr;
std::atomic<bool> g_test_running{false};
std::thread g_test_thread, g_log_thread;
std::atomic<uint32_t> g_polls[4];
std::atomic<uint64_t> g_poll_total{0};

bool OurWindowIsForeground() {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
}

std::filesystem::path LogPath() {
  auto dir = rex::filesystem::GetExecutableFolder() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir / "pcinput.txt";
}

void Log(const char* fmt, ...) {
  static std::mutex m;
  static double t0 = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  std::lock_guard<std::mutex> lock(m);
  static FILE* f = _wfopen(LogPath().c_str(), L"w");
  if (!f) return;
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - t0;
  fprintf(f, "[%7.1f] ", t);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fflush(f);
}

// Status line every 5 s: lets a test run (or a bug report) show whether the
// window events arrive and the pad is being driven.
void LogThread() {
  int n = 0;
  while (g_test_running || g_input.Enabled()) {
    for (int i = 0; i < 50 && (g_test_running || g_input.Enabled()); ++i) Sleep(100);
    char s[256];
    g_input.Stats(s, sizeof(s));
    Log("%s fg=%d polls/5s u0=%u u1=%u u2=%u u3=%u", s, OurWindowIsForeground() ? 1 : 0,
        g_polls[0].exchange(0), g_polls[1].exchange(0), g_polls[2].exchange(0), g_polls[3].exchange(0));
    if (++n > 100000) break;
  }
}

// Automated test: real OS input through SendInput, so it travels the same way
// as a physical mouse/keyboard (raw input -> SDL -> window events -> here).
void SendMouseMove(int dx, int dy) {
  if (!OurWindowIsForeground()) return;  // never type into another program
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dx = dx;
  in.mi.dy = dy;
  in.mi.dwFlags = MOUSEEVENTF_MOVE;
  SendInput(1, &in, sizeof(in));
}
void SendKey(WORD vk, bool down) {
  if (down && !OurWindowIsForeground()) return;  // never type into another program
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
  in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
  SendInput(1, &in, sizeof(in));
}
void SendButton(bool down) {
  if (down && !OurWindowIsForeground()) return;  // never type into another program
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  SendInput(1, &in, sizeof(in));
}

void TestThread() {
  for (int i = 0; i < 1500 && g_test_running; ++i) Sleep(100);  // 150 s: in game
  auto wait_fg = [] {
    for (int i = 0; i < 50 && g_test_running && !OurWindowIsForeground(); ++i) Sleep(100);
    return OurWindowIsForeground();
  };
  Log("test: starting (foreground=%d)", OurWindowIsForeground() ? 1 : 0);
  // 1) Look right ~2 s, then left ~2 s, then up/down.
  for (int phase = 0; phase < 4 && g_test_running; ++phase) {
    if (!wait_fg()) Log("test: window not in foreground");
    const int dx = phase == 0 ? 12 : phase == 1 ? -12 : 0;
    const int dy = phase == 2 ? -8 : phase == 3 ? 8 : 0;
    Log("test: mouse phase %d dx=%d dy=%d", phase, dx, dy);
    for (int i = 0; i < 200 && g_test_running; ++i) {
      SendMouseMove(dx, dy);
      Sleep(10);
    }
    Sleep(1000);
  }
  // 2) Walk forward 2 s (W), then fire 5 times (LMB), reload (R), flashlight (F).
  Log("test: hold W");
  SendKey('W', true);
  Sleep(2000);
  SendKey('W', false);
  Sleep(800);
  for (int i = 0; i < 5 && g_test_running; ++i) {
    Log("test: click LMB %d", i);
    SendButton(true);
    Sleep(120);
    SendButton(false);
    Sleep(500);
  }
  Log("test: press R");
  SendKey('R', true); Sleep(150); SendKey('R', false);
  Sleep(2500);
  Log("test: press F");
  SendKey('F', true); Sleep(150); SendKey('F', false);
  Sleep(1500);
  Log("test: done");
}

// Calibration: hold right-stick X at fixed levels (fractions of full scale)
// for 3 s each with 2 s rests; autoshots show which levels turn the view.
void CalibrateThread() {
  for (int i = 0; i < 1400 && g_test_running; ++i) Sleep(100);  // 140 s
  static const double kLevels[] = {0.16, 0.20, 0.23, 0.25, 0.27, 0.29, 0.32, 0.36, 0.45, 0.60};
  Log("calibrate: start");
  for (double l : kLevels) {
    if (!g_test_running) break;
    g_cal_rx = int(l * 32767.0);
    Log("calibrate: level %.2f (%d) on", l, int(l * 32767.0));
    Sleep(3000);
    g_cal_rx = 0;
    Log("calibrate: level %.2f off", l);
    Sleep(2000);
  }
  g_cal_rx = INT32_MIN;
  Log("calibrate: done");
}

}  // namespace

namespace l4d2 {

bool PcControlsEnabled() {
  return REXCVAR_GET(l4d2_pc_controls) || REXCVAR_GET(l4d2_raw_mouse);
}

void AttachPcInput(rex::ui::Window* window) {
  if (!window || g_window) return;
  g_window = window;
  g_input.SetEnabled(PcControlsEnabled());
  // Same z-order as the SDK's keyboard/mouse driver; added later, so these
  // events arrive first. Overlays (F4 settings, console) sit above and keep
  // the mouse while they are open.
  window->AddInputListener(&g_input, 0);
  Log("pc controls %s", g_input.Enabled() ? "on" : "off");
  if (REXCVAR_GET(l4d2_test_input)) {
    g_test_running = true;
    g_test_thread = std::thread(TestThread);
  } else if (REXCVAR_GET(l4d2_test_calibrate)) {
    g_test_running = true;
    g_test_thread = std::thread(CalibrateThread);
  }
  g_log_thread = std::thread(LogThread);
}

void DetachPcInput() {
  g_test_running = false;
  g_input.SetEnabled(false);
  if (g_test_thread.joinable()) g_test_thread.join();
  if (g_log_thread.joinable()) g_log_thread.join();
  if (g_window) g_window->RemoveInputListener(&g_input);
  g_window = nullptr;
}

void PcInputLog(const char* msg) { Log("%s", msg); }

}  // namespace l4d2

// Called by the inputsystem hook after every XInputGetState(user, state).
// state points at the guest X_INPUT_STATE (packet(4) buttons(2) lt rt lx ly
// rx ry, big-endian). Returns 1 when it changed the state.
extern "C" __declspec(dllexport) int L4d2PcEditPadState(uint32_t user, uint8_t* state) {
  if (user < 4) g_polls[user]++;
  g_poll_total++;
  if (user != 0 || !state) return 0;
  int changed = 0;
  static bool pad_pressed = false;  // a real controller button was pressed
  if (state[4] | state[5]) pad_pressed = true;
  // Focus: the window's own flag (input stops when you alt-tab away).
  const bool focused = g_window ? g_window->HasFocus() : OurWindowIsForeground();
  if (g_input.Edit(state, focused)) changed = 1;
  if (const int cal = g_cal_rx.load(); cal != INT32_MIN) {
    state[12] = uint8_t(uint16_t(cal) >> 8); state[13] = uint8_t(uint16_t(cal));
    state[14] = 0; state[15] = 0;
    changed = 1;
  }
  if (REXCVAR_GET(l4d2_skip_press_start)) {
    // Skip Intro: with -novid the title screen comes up a few seconds after
    // boot. A short START pulse every 1.5 s from 4 s to 40 s goes through the
    // normal sign-in (profile + storage device, so saving works). START on the
    // loading screens and on the main menu does nothing for player one. It
    // stops for good as soon as the player presses anything themselves.
    static const auto t0 = std::chrono::steady_clock::now();
    static bool done = false;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!done && (t > 40.0 || g_input.UserActive() || pad_pressed)) {
      done = true;
      Log("skip press start: stopped at %.1f s", t);
    }
    if (!done && t >= 4.0 && std::fmod(t - 4.0, 1.5) < 0.15) {
      state[5] |= 0x10;  // START
      changed = 1;
    }
  }
  if (REXCVAR_GET(l4d2_test_autopress)) {
    // Test autopilot: title -> START, main menu -> A (Play), A (Start game).
    static const auto start = std::chrono::steady_clock::now();
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    static const struct { double at; uint16_t button; } kSchedule[] = {
        {30, 0x0010}, {36, 0x0010}, {42, 0x1000}, {48, 0x1000}, {54, 0x1000},
    };
    for (const auto& s : kSchedule) {
      if (t >= s.at && t < s.at + 0.25) {
        state[4] |= uint8_t(s.button >> 8);
        state[5] |= uint8_t(s.button);
        changed = 1;
      }
    }
  }
  return changed;
}

// ---------------------------------------------------------------------------
// Frame-rate cap. The Xbox 360 build limits itself with the engine ConVar
// fps_max (default "30", flagged development-only so the console can't change
// it). The inputsystem hook writes the chosen value into that ConVar.
REXCVAR_DEFINE_INT32(l4d2_fps_max, 30, "L4D2 Display",
                     "Game frame-rate cap: 30 = Xbox 360 default, 60/120/144..., 0 = unlimited");

extern "C" __declspec(dllexport) int L4d2FpsMax() { return REXCVAR_GET(l4d2_fps_max); }

// ---------------------------------------------------------------------------
// Field of view (FOV Changer mod: "l4d2_fov {fov}"). The game clamps
// fov_desired to 75-90; src/stubs/hook_client.cpp instead scales the client's
// final view FOV so that the default 90 becomes this value.
REXCVAR_DEFINE_INT32(l4d2_fov, 0, "L4D2 Display", "Field of view in degrees (0 = game default, 90)");

extern "C" __declspec(dllexport) int L4d2FovOverride() { return REXCVAR_GET(l4d2_fov); }
