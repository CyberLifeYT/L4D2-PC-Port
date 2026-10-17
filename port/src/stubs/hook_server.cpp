// server overrides: field of view.
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
}  // namespace

// sub_89A7E190: CBasePlayer::SetDefaultFOV(int fov). The caller reads the
// player's fov_desired and clamps it to 75-90, so a wider view is impossible
// through the game's own settings. This is the same function with one change:
// when the port's l4d2_fov is set (FOV Changer mod), that value is used.
//
//   if (fov == 0) fov = g_pGameRules->DefaultFOV();      // vtable +0x74
//   if (m_iDefaultFOV == fov) return;
//   NetworkStateChanged(&m_iDefaultFOV);                  // edict change info
//   m_iDefaultFOV = fov;                                  // player + 0x1D58
REX_HOOK_RAW(sub_89A7E190) {
  const uint32_t player = ctx.r3.u32;
  int32_t fov = int32_t(ctx.r4.u32);
  if (FovFn fn = FovOverride()) {
    const int o = fn();
    if (o >= 50 && o <= 150) fov = o;
  }
  if (fov == 0) {
    const uint32_t rules = Load32(base + 0x89F14624);
    if (rules) {
      const uint32_t vt = Load32(base + rules);
      if (PPCFunc* f = rex::runtime::ResolveIndirectFunction(Load32(base + vt + 0x74))) {
        PPCContext saved = ctx;
        ctx.r3.u64 = rules;
        f(ctx, base);
        fov = int32_t(ctx.r3.u32);
        ctx = saved;
      }
    }
  }
  const uint32_t field = player + 0x1D58;
  if (int32_t(Load32(base + field)) == fov) return;
  if (base[field - 0x1CF8]) {
    base[field - 0x1CF4] |= 0x80;
  } else if (const uint32_t info = Load32(base + field - 0x1D34)) {
    Store32(base + info, Load32(base + info) | 0x101);
    PPCContext saved = ctx;
    ctx.r3.u64 = info;
    sub_899E59F0(ctx, base);
    const uint32_t r = ctx.r3.u32;
    ctx = saved;
    base[r + 2] = 0;
    base[r + 3] = 0;
  }
  Store32(base + field, uint32_t(fov));
}
