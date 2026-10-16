// vscript_360: 11 import slots (2 data, 9 function) from guest DLLs.
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
    {0x84800400, 0x82C11CB0},  // tier0_360 #92
    {0x84800404, 0x82C11E08},  // tier0_360 #105
    {0x84800408, 0x82C11EA8},  // tier0_360 #106
    {0x8480040C, 0x82C11CB0},  // tier0_360 #90
    {0x84800410, 0x82C121E8},  // tier0_360 #111
    {0x84800414, 0x82C18538},  // tier0_360 #292
    {0x84800418, 0x82C11F48},  // tier0_360 #107
    {0x8480041C, 0x82C12148},  // tier0_360 #110
    {0x84800420, 0x82C406CC},  // tier0_360 #234 (data)
    {0x84800428, 0x82E1BF68},  // vstdlib_360 #19
    {0x8480042C, 0x82E1BF38},  // vstdlib_360 #17 (data)
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
    REXLOG_INFO("import fixup: vscript_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
