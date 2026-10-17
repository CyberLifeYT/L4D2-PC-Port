// client overrides: field of view (FOV Changer mod, l4d2_fov).
#include "l4d2_funcs.h"

#include <rex/hook.h>
#include <rex/ppc/func.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
uint32_t Load32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void Store32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
using FovFn = int (*)();
FovFn FovOverride() {
  static FovFn fn = reinterpret_cast<FovFn>(GetProcAddress(GetModuleHandleW(nullptr), "L4d2FovOverride"));
  return fn;
}

// Widen (or narrow) the view. The game's view FOV is relative to its default
// of 90, so 90 becomes l4d2_fov and zoomed views (scopes) keep the same
// magnification relative to it (scaled through the tangent of the half angle).
void ApplyFov(uint8_t* base, uint32_t fov_addr) {
  FovFn fn = FovOverride();
  if (!fn || !fov_addr) return;
  const int want = fn();
  if (want < 50 || want > 150 || want == 90) return;
  uint32_t bits = Load32(base + fov_addr);
  float cur;
  std::memcpy(&cur, &bits, 4);
  if (!(cur > 1.0f && cur < 179.0f)) return;
  constexpr double kHalfRad = 3.14159265358979323846 / 360.0;  // degrees -> half angle in radians
  const double t = std::tan(cur * kHalfRad) * std::tan(want * kHalfRad);  // divided by tan(45 deg) = 1
  const float out = float(2.0 * std::atan(t) / kHalfRad);
  {
    using LogFn = void (*)(const char*);
    static LogFn log = reinterpret_cast<LogFn>(GetProcAddress(GetModuleHandleW(nullptr), "L4d2ConsoleLog"));
    static int n = 0;
    static uint64_t calls = 0;
    ++calls;
    if (log && (n < 40 || calls % 600 == 0)) {
      ++n;
      char m[160];
      snprintf(m, sizeof(m), "fov hook #%llu addr %08X cur %.3f want %d out %.3f", (unsigned long long)calls, fov_addr,
               cur, want, out);
      log(m);
    }
  }
  std::memcpy(&bits, &out, 4);
  Store32(base + fov_addr, bits);
}
}  // namespace

// sub_88585420: ToolFramework_SetupEngineView(Vector& origin, QAngle& angles,
// float& fov). CViewRender::SetUpView calls it (its only caller) right after
// the player's CalcView and the client mode's OverrideView have produced the
// final view FOV, so this is where the port applies l4d2_fov. The rest is the
// original function:
//   if (!g_pToolFramework) return false;
//   return g_pToolFramework->SetupEngineView(origin, angles, fov);  // vtable +0x24
REX_HOOK_RAW(sub_88585420) {
  ApplyFov(base, ctx.r5.u32);
  constexpr uint32_t kToolFramework = 0x8875C374 + 4;
  const uint32_t tools = Load32(base + kToolFramework);
  PPCFunc* f = tools ? rex::runtime::ResolveIndirectFunction(Load32(base + Load32(base + tools) + 0x24)) : nullptr;
  if (!f) {
    ctx.r3.u64 = 0;
    return;
  }
  ctx.r6.u64 = ctx.r5.u64;
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.u64 = tools;
  f(ctx, base);
}
