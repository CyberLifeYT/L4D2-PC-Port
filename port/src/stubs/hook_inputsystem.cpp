// inputsystem overrides: PC keyboard & mouse, mod console commands, fps cap.
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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// sub_83513798 is the game's XInputGetState(dwUserIndex, pState) wrapper
// (mr r5,r4 / li r4,1 / b XamInputGetState). After the real call the port's
// PC keyboard & mouse controls are merged into pad 0 (src/pcinput.cpp, in
// l4d2.exe), and queued console commands from mods are handed to the engine's
// Cbuf_AddText (src/console.cpp).
namespace {
using EditFn = int (*)(uint32_t, uint8_t*);
using NextCmdFn = int (*)(char*, int);
using BaseFn = void (*)(uint8_t*);
using LogFn = void (*)(const char*);
template <typename T>
T Export(const char* name) {
  return reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(nullptr), name));
}
void BumpPacket(uint8_t* p) {
  uint32_t pkt = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  ++pkt;
  p[0] = uint8_t(pkt >> 24); p[1] = uint8_t(pkt >> 16); p[2] = uint8_t(pkt >> 8); p[3] = uint8_t(pkt);
}
uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// engine_360 Cbuf_AddText(ECommandTarget_t target, const char* text, int arg)
// at 0x86BCDDF0 (found from its "Cbuf_AddText: buffer overflow" message).
// Its first instructions are checked before every use.
constexpr uint32_t kCbufAddText = 0x86BCDDF0;
bool CbufLooksRight(uint8_t* base) {
  const uint8_t* p = base + kCbufAddText;
  return Be32(p) == 0x7D8802A6 /* mflr r12 */ && Be32(p + 8) == 0x9421FF80 /* stwu r1,-0x80(r1) */ &&
         Be32(p + 12) == 0x7C7D1B78 /* mr r29,r3 */;
}

void RunConsoleCommands(PPCContext& ctx, uint8_t* base) {
  static NextCmdFn next = Export<NextCmdFn>("L4d2NextConsoleCommand");
  static LogFn log = Export<LogFn>("L4d2ConsoleLog");
  if (!next) return;
  static PPCFunc* fn = nullptr;
  static bool checked = false;
  char cmd[512];
  for (int n = 0; n < 8 && next(cmd, sizeof(cmd)); ++n) {
    if (!checked) {
      checked = true;
      if (CbufLooksRight(base)) fn = rex::runtime::ResolveIndirectFunction(kCbufAddText);
      if (log) log(fn ? "Cbuf_AddText found" : "Cbuf_AddText not found - console commands disabled");
    }
    if (!fn) return;
    // Guest call: string in a fresh frame below the caller's stack, then
    // Cbuf_AddText(0 = player 1's buffer, text, 0). Every register is restored.
    PPCContext saved = ctx;
    const uint32_t len = uint32_t(strlen(cmd)) + 1;
    const uint32_t sp = (ctx.r1.u32 - 0x100 - ((len + 15) & ~15u)) & ~15u;
    memcpy(base + sp + 0x80, cmd, len);
    ctx.r1.u64 = sp;
    ctx.r3.u64 = 0;
    ctx.r4.u64 = sp + 0x80;
    ctx.r5.u64 = 0;
    fn(ctx, base);
    ctx = saved;
    // Log each distinct command once (mod commands and key binds repeat every 10 s).
    static std::set<std::string> logged;
    if (log && logged.size() < 2000 && logged.insert(cmd).second) {
      char m[600];
      snprintf(m, sizeof(m), "ran: %s", cmd);
      if (char* nl = strchr(m, '\n')) *nl = 0;
      log(m);
    }
  }
}
}  // namespace

// Developer aid (L4D2_DUMP_IMAGES=1): writes guest memory of the module
// images to logs\image_*.bin for offline analysis - engine at the first pad
// poll, the client/server/matchmaking area once the game has been running
// for two minutes (those DLLs load later).
static void DumpRange(uint8_t* base, const char* name, uint32_t lo, uint32_t hi) {
  char path[MAX_PATH];
  snprintf(path, sizeof(path), "logs\\image_%s.bin", name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  // Copy committed pages; reserved/free gaps are written as zeros.
  static uint8_t zeros[0x10000];
  for (uint32_t a = lo; a < hi;) {
    MEMORY_BASIC_INFORMATION mbi{};
    uint32_t n = 0x1000;
    if (VirtualQuery(base + a, &mbi, sizeof(mbi))) {
      uint64_t end = uint64_t(reinterpret_cast<uint8_t*>(mbi.BaseAddress) - base) + mbi.RegionSize;
      n = uint32_t(std::min<uint64_t>(end, hi) - a);
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

static void DumpImages(uint8_t* base) {
  static int stage = -1;
  static ULONGLONG t0 = 0;
  if (stage == -1) {
    char flag[8] = {};
    stage = (GetEnvironmentVariableA("L4D2_DUMP_IMAGES", flag, sizeof(flag)) && flag[0] == '1') ? 0 : 2;
    t0 = GetTickCount64();
  }
  if (stage == 0) {
    DumpRange(base, "engine_86B00000", 0x86B00000, 0x871D0000);
    stage = 1;
  } else if (stage == 1 && GetTickCount64() - t0 > 120000) {
    DumpRange(base, "client_88000000", 0x88000000, 0x8A000000);
    DumpRange(base, "gameui_84000000", 0x84000000, 0x84800000);
    DumpRange(base, "inputsystem_83500000", 0x83500000, 0x83680000);
    stage = 2;
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
  static BaseFn set_base = Export<BaseFn>("L4d2SetGuestBase");
  static EditFn edit = Export<EditFn>("L4d2PcEditPadState");
  static bool once = [&] { if (set_base) set_base(base); return true; }();
  (void)once;
  DumpImages(base);
  ApplyFpsMax(base);
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.s64 = 1;
  __imp__XamInputGetState(ctx, base);
  const bool ok = ctx.r3.u32 == 0 && state != 0;
  if (edit && edit(user, ok ? base + state : nullptr) && ok) BumpPacket(base + state);
  if (user == 0) {
    const uint64_t r3 = ctx.r3.u64;
    RunConsoleCommands(ctx, base);
    ctx.r3.u64 = r3;
  }
}
