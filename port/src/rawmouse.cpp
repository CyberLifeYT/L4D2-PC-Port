// L4D2 PC port - raw mouse input (WM_INPUT) for 1:1 PC aiming.
//
// The Xbox 360 game only reads XInput pads; the SDK's keyboard/mouse mode turns
// mouse motion into a right-stick deflection, which the game then runs through
// its stick response curve (deadzone + acceleration). That feels floaty.
//
// Here the exe collects raw mouse counts with RegisterRawInputDevices (no
// Windows pointer acceleration, no cursor clamping) and the inputsystem hook
// (src/stubs/hook_inputsystem.cpp) converts them, at the moment the game polls
// the pad, into a stick value that is linear in mouse speed and pre-compensated
// for the stick deadzone - so a given mouse movement always turns the view by
// the same amount, regardless of frame rate.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(l4d2_raw_mouse, false, "L4D2 Input",
                    "Raw (WM_INPUT) mouse aiming replaces the emulated right stick");
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_sensitivity, 1.0, "L4D2 Input", "Raw mouse sensitivity multiplier");
REXCVAR_DEFINE_BOOL(l4d2_mouse_invert_y, false, "L4D2 Input", "Invert raw mouse vertical aim");
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_deadzone, 0.24, "L4D2 Input",
                      "Stick deadzone the game applies (0-0.5); raw mouse output starts just past it");
REXCVAR_DEFINE_INT32(l4d2_test_mouse_sweep, 0, "L4D2 Debug",
                     "Automated tests: inject N mouse counts per second of yaw after 150 s (0 = off)");
REXCVAR_DEFINE_DOUBLE(l4d2_mouse_counts_full, 2400.0, "L4D2 Input",
                      "Mouse counts per second that equal a full stick deflection at sensitivity 1");

namespace {
std::atomic<int64_t> g_dx{0}, g_dy{0};
std::atomic<bool> g_running{false};
std::thread g_thread;
HWND g_hwnd = nullptr;

bool OurWindowIsForeground() {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_INPUT) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
        ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
        OurWindowIsForeground()) {
      g_dx += ri.data.mouse.lLastX;
      g_dy += ri.data.mouse.lLastY;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void Thread() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"L4D2RawMouse";
  RegisterClassW(&wc);
  g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;  // generic desktop
  rid.usUsage = 0x02;      // mouse
  rid.dwFlags = RIDEV_INPUTSINK;  // receive while another of our windows has focus
  rid.hwndTarget = g_hwnd;
  RegisterRawInputDevices(&rid, 1, sizeof(rid));
  MSG msg;
  while (g_running && GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
  if (g_hwnd) DestroyWindow(g_hwnd);
}
}  // namespace

namespace l4d2 {
void StartRawMouse() {
  if (!REXCVAR_GET(l4d2_raw_mouse) || g_running.exchange(true)) return;
  g_thread = std::thread(Thread);
}
void StopRawMouse() {
  if (!g_running.exchange(false)) return;
  if (g_hwnd) PostMessageW(g_hwnd, WM_QUIT, 0, 0);
  if (g_thread.joinable()) g_thread.join();
}
}  // namespace l4d2

// Called by the inputsystem hook each time the game polls pad 0. Fills the
// right-stick values (-32767..32767, Xbox convention: +y is up) from the mouse
// counts accumulated since the previous poll. Returns 0 when raw mouse is off.
extern "C" __declspec(dllexport) int L4d2RawMouseStick(int16_t* out_rx, int16_t* out_ry) {
  if (!g_running) return 0;
  static LARGE_INTEGER freq{}, last{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  double dt = last.QuadPart ? double(now.QuadPart - last.QuadPart) / double(freq.QuadPart) : 1.0 / 30.0;
  last = now;
  // Polls closer than 2 ms apart (several reads per frame) share one interval.
  dt = std::clamp(dt, 0.002, 0.1);
  double dx = double(g_dx.exchange(0)), dy = double(g_dy.exchange(0));
  if (const int sweep = REXCVAR_GET(l4d2_test_mouse_sweep)) {
    static const LARGE_INTEGER t0 = now;
    if (double(now.QuadPart - t0.QuadPart) / double(freq.QuadPart) > 150.0) dx += sweep * dt;
  }
  const double full = std::max(1.0, REXCVAR_GET(l4d2_mouse_counts_full)) / std::max(0.01, REXCVAR_GET(l4d2_mouse_sensitivity));
  const double dz = std::clamp(REXCVAR_GET(l4d2_mouse_deadzone), 0.0, 0.5);
  auto shape = [&](double counts) -> int16_t {
    double v = counts / dt / full;  // linear in mouse speed
    if (std::fabs(v) < 1e-4) return 0;
    double mag = dz + (1.0 - dz) * std::min(1.0, std::fabs(v));  // skip the deadzone
    return int16_t(std::copysign(mag, v) * 32767.0);
  };
  *out_rx = shape(dx);
  *out_ry = shape(REXCVAR_GET(l4d2_mouse_invert_y) ? dy : -dy);
  return 1;
}

// ---------------------------------------------------------------------------
// Test autopilot: presses START then A on a fixed schedule so automated test
// runs (scripts\test_*.bat) get from the title screen into a campaign without
// anyone at the controller: title -> START, main menu -> A (Play single
// player), single player -> A (Start game). Off by default.
REXCVAR_DEFINE_BOOL(l4d2_test_autopress, false, "L4D2 Debug",
                    "Automated tests: press START/A on a schedule to reach gameplay");

extern "C" __declspec(dllexport) uint16_t L4d2AutoButtons() {
  if (!REXCVAR_GET(l4d2_test_autopress)) return 0;
  static const auto start = std::chrono::steady_clock::now();
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  // (time s, button) - XINPUT_GAMEPAD_START 0x0010, A 0x1000.
  static const struct { double at; uint16_t button; } kSchedule[] = {
      {30, 0x0010}, {36, 0x0010}, {42, 0x1000}, {48, 0x1000}, {54, 0x1000},
  };
  for (const auto& s : kSchedule) {
    if (t >= s.at && t < s.at + 0.25) return s.button;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Frame-rate cap. The Xbox 360 build limits itself with the engine ConVar
// fps_max (default "30", flagged development-only so the console can't change
// it). The hook writes the chosen value into that ConVar each frame.
REXCVAR_DEFINE_INT32(l4d2_fps_max, 30, "L4D2 Display",
                     "Game frame-rate cap: 30 = Xbox 360 default, 60/120/144..., 0 = unlimited");

extern "C" __declspec(dllexport) int L4d2FpsMax() { return REXCVAR_GET(l4d2_fps_max); }
