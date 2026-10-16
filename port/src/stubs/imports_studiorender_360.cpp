// studiorender_360: 17 import slots (1 data, 16 function) from guest DLLs.
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
    {0x86380400, 0x82C12318},  // tier0_360 #113
    {0x86380404, 0x82C11CB0},  // tier0_360 #90
    {0x86380408, 0x82C11CB0},  // tier0_360 #92
    {0x8638040C, 0x82C406CC},  // tier0_360 #234 (data)
    {0x86380410, 0x82C123B8},  // tier0_360 #114
    {0x86380414, 0x82C19100},  // tier0_360 #506
    {0x86380418, 0x82C190B0},  // tier0_360 #509
    {0x8638041C, 0x82C197A8},  // tier0_360 #565
    {0x86380420, 0x82C18B18},  // tier0_360 #475
    {0x86380424, 0x82C11F48},  // tier0_360 #107
    {0x86380428, 0x82C11EA8},  // tier0_360 #106
    {0x8638042C, 0x82C19028},  // tier0_360 #505
    {0x86380430, 0x82C11E08},  // tier0_360 #105
    {0x86380434, 0x82C19000},  // tier0_360 #504
    {0x8638043C, 0x82E1BF68},  // vstdlib_360 #19
    {0x86380440, 0x82E1BF38},  // vstdlib_360 #17
    {0x86380444, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: studiorender_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
