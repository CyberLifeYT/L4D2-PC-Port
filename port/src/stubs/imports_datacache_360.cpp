// datacache_360: 26 import slots (3 data, 23 function) from guest DLLs.
// Generated from the XEX import/export tables. Real Xbox loader behaviour:
// write each import slot with the provider's export address before the
// module's entry point runs. Without this, data imports such as tier0's
// g_pMemAlloc read as 0 and every allocation calls address 0.
#include "l4d2_pch.h"

#include <rex/hook.h>

#include <windows.h>
#include <iterator>

namespace {
struct ImportSlot { uint32_t slot; uint32_t value; };
const ImportSlot kSlots[] = {
    {0x83280400, 0x82C11CB0},  // tier0_360 #90
    {0x83280404, 0x82C11CB0},  // tier0_360 #92
    {0x83280408, 0x82C192C0},  // tier0_360 #519
    {0x8328040C, 0x82C11CB0},  // tier0_360 #520
    {0x83280410, 0x82C10428},  // tier0_360 #23
    {0x83280414, 0x82C10138},  // tier0_360 #24
    {0x83280418, 0x82C19000},  // tier0_360 #504
    {0x8328041C, 0x82C19028},  // tier0_360 #505
    {0x83280420, 0x82C19100},  // tier0_360 #506
    {0x83280424, 0x82C19050},  // tier0_360 #507
    {0x83280428, 0x82C190B0},  // tier0_360 #509
    {0x8328042C, 0x82C197A8},  // tier0_360 #565
    {0x83280430, 0x82C18B18},  // tier0_360 #475
    {0x83280434, 0x82C406CC},  // tier0_360 #234 (data)
    {0x83280438, 0x82C11F48},  // tier0_360 #107
    {0x8328043C, 0x82C12148},  // tier0_360 #110
    {0x83280440, 0x82C18FD0},  // tier0_360 #502
    {0x83280444, 0x82C18B10},  // tier0_360 #474
    {0x83280448, 0x82C649BC},  // tier0_360 #604 (data)
    {0x8328044C, 0x82C11E08},  // tier0_360 #105
    {0x83280450, 0x82C11EA8},  // tier0_360 #106
    {0x83280454, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x83280458, 0x82C121E8},  // tier0_360 #111
    {0x8328045C, 0x82C18BB8},  // tier0_360 #480
    {0x83280460, 0x82C185D8},  // tier0_360 #293
    {0x83280464, 0x82C11FE8},  // tier0_360 #108
};

void FillImportSlots(uint8_t* base) {
  for (const auto& s : kSlots) {
    uint8_t* p = base + s.slot;
    DWORD old = 0;
    BOOL prot = VirtualProtect(p, 4, PAGE_READWRITE, &old);
    p[0] = uint8_t(s.value >> 24);
    p[1] = uint8_t(s.value >> 16);
    p[2] = uint8_t(s.value >> 8);
    p[3] = uint8_t(s.value);
    if (prot) VirtualProtect(p, 4, old, &old);
  }
}
}  // namespace

extern "C" REX_FUNC(__imp__xstart);

REX_HOOK_RAW(xstart) {
  static bool logged = false;
  FillImportSlots(base);
  if (!logged) {
    logged = true;
    REXLOG_INFO("import fixup: datacache_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
