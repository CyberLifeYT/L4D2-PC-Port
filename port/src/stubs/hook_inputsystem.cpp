// inputsystem overrides: raw mouse aiming.
#include "l4d2_funcs.h"

#include <rex/hook.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

// sub_83513798 is the game's XInputGetState(dwUserIndex, pState) wrapper
// (mr r5,r4 / li r4,1 / b XamInputGetState). After the real call, when the
// port's raw-mouse mode is on, pad 0's right stick is replaced with the
// linear, deadzone-compensated value computed from raw mouse counts
// (src/rawmouse.cpp, exported by l4d2.exe).
namespace {
using StickFn = int (*)(int16_t*, int16_t*);
using ButtonsFn = uint16_t (*)();
StickFn RawMouse() {
  static StickFn fn = reinterpret_cast<StickFn>(GetProcAddress(GetModuleHandleW(nullptr), "L4d2RawMouseStick"));
  return fn;
}
ButtonsFn AutoButtons() {
  static ButtonsFn fn = reinterpret_cast<ButtonsFn>(GetProcAddress(GetModuleHandleW(nullptr), "L4d2AutoButtons"));
  return fn;
}
void BumpPacket(uint8_t* p) {
  uint32_t pkt = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  ++pkt;
  p[0] = uint8_t(pkt >> 24); p[1] = uint8_t(pkt >> 16); p[2] = uint8_t(pkt >> 8); p[3] = uint8_t(pkt);
}
}  // namespace

// Debug: with L4D2_DUMP_IMAGES=1 set, the first pad poll writes the loaded
// (decrypted, decompressed) engine/client/server images to logs\image_*.bin so
// they can be inspected with REA/Ghidra. Never active for players.
static void DumpImagesOnce(uint8_t* base) {
  static bool done = false;
  if (done) return;
  done = true;
  char flag[8] = {};
  if (!GetEnvironmentVariableA("L4D2_DUMP_IMAGES", flag, sizeof(flag)) || flag[0] != '1') return;
  struct { const char* name; uint32_t lo, hi; } ranges[] = {
      {"engine_86B00000", 0x86B00000, 0x871D0000},
      {"guest_88000000", 0x88000000, 0x8A000000},
  };
  for (auto& r : ranges) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "logs\\image_%s.bin", r.name);
    if (FILE* f = fopen(path, "wb")) {
      // Copy committed pages; reserved/free gaps are written as zeros.
      static uint8_t zeros[0x10000];
      for (uint32_t a = r.lo; a < r.hi;) {
        MEMORY_BASIC_INFORMATION mbi{};
        uint32_t n = 0x1000;
        if (VirtualQuery(base + a, &mbi, sizeof(mbi))) {
          uint64_t end = uint64_t(reinterpret_cast<uint8_t*>(mbi.BaseAddress) - base) + mbi.RegionSize;
          n = uint32_t(std::min<uint64_t>(end, r.hi) - a);
          if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            fwrite(base + a, 1, n, f);
            a += n;
            continue;
          }
        }
        for (uint32_t k = 0; k < n; k += sizeof(zeros)) fwrite(zeros, 1, std::min<uint32_t>(sizeof(zeros), n - k), f);
        a += n;
      }
      fclose(f);
    }
  }
}

// engine_360's "fps_max" ConVar (this XEX build): name at +0x0C, m_fValue at
// +0x2C (big-endian float), m_nValue at +0x30. Checked before every write.
static void ApplyFpsMax(uint8_t* base) {
  using FpsFn = int (*)();
  static FpsFn fn = reinterpret_cast<FpsFn>(GetProcAddress(GetModuleHandleW(nullptr), "L4d2FpsMax"));
  if (!fn) return;
  constexpr uint32_t kConVar = 0x86F8D948, kName = 0x86B195CC;
  uint8_t* cv = base + kConVar;
  const uint32_t name = (uint32_t(cv[0xC]) << 24) | (uint32_t(cv[0xD]) << 16) | (uint32_t(cv[0xE]) << 8) | cv[0xF];
  if (name != kName || memcmp(base + kName, "fps_max", 8) != 0) return;
  const int want = std::clamp(fn(), 0, 1000);
  float f = float(want);
  uint32_t fb;
  memcpy(&fb, &f, 4);
  const uint32_t ib = uint32_t(want);
  uint8_t v[8] = {uint8_t(fb >> 24), uint8_t(fb >> 16), uint8_t(fb >> 8), uint8_t(fb),
                  uint8_t(ib >> 24), uint8_t(ib >> 16), uint8_t(ib >> 8), uint8_t(ib)};
  if (memcmp(cv + 0x2C, v, 8) != 0) memcpy(cv + 0x2C, v, 8);
}

REX_HOOK_RAW(sub_83513798) {
  DumpImagesOnce(base);
  ApplyFpsMax(base);
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.s64 = 1;
  __imp__XamInputGetState(ctx, base);
  if (user != 0 || ctx.r3.u32 != 0 || !state) return;
  if (StickFn fn = RawMouse()) {
    int16_t rx, ry;
    if (fn(&rx, &ry)) {
      // X_INPUT_STATE: packet(4) buttons(2) lt rt lx(2) ly(2) rx(2) ry(2), big-endian.
      uint8_t* p = base + state;
      const uint16_t urx = uint16_t(rx), ury = uint16_t(ry);
      p[12] = uint8_t(urx >> 8); p[13] = uint8_t(urx);
      p[14] = uint8_t(ury >> 8); p[15] = uint8_t(ury);
      BumpPacket(p);  // so the game treats the pad as changed
    }
  }
  if (ButtonsFn fn = AutoButtons()) {
    if (uint16_t b = fn()) {
      uint8_t* p = base + state;
      p[4] |= uint8_t(b >> 8);
      p[5] |= uint8_t(b);
      BumpPacket(p);
    }
  }
}
