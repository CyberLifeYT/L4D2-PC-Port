// inputsystem_360: 12 import slots (1 data, 11 function) from guest DLLs.
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
    {0x83500400, 0x82C22628},  // tier0_360 #841
    {0x83500404, 0x82C22560},  // tier0_360 #838
    {0x83500408, 0x82C228F0},  // tier0_360 #846
    {0x8350040C, 0x82C12318},  // tier0_360 #113
    {0x83500410, 0x82C11CB0},  // tier0_360 #90
    {0x83500414, 0x82C11CB0},  // tier0_360 #92
    {0x83500418, 0x82C21B58},  // tier0_360 #798
    {0x8350041C, 0x82C21CF0},  // tier0_360 #811
    {0x83500420, 0x82C11EA8},  // tier0_360 #106
    {0x83500424, 0x82C21B48},  // tier0_360 #797
    {0x83500428, 0x82C22978},  // tier0_360 #848
    {0x8350042C, 0x82C406CC},  // tier0_360 #234 (data)
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
    REXLOG_INFO("import fixup: inputsystem_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
