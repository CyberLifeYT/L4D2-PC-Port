// stdshader_dx9_360: 6 import slots (1 data, 5 function) from guest DLLs.
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
    {0x85F00400, 0x82C406CC},  // tier0_360 #234 (data)
    {0x85F00404, 0x82C11CB0},  // tier0_360 #90
    {0x85F00408, 0x82C11CB0},  // tier0_360 #92
    {0x85F0040C, 0x82C12148},  // tier0_360 #110
    {0x85F00410, 0x82C11EA8},  // tier0_360 #106
    {0x85F00414, 0x82C10CD8},  // tier0_360 #48
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
    REXLOG_INFO("import fixup: stdshader_dx9_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
