// soundemittersystem_360: 28 import slots (1 data, 27 function) from guest DLLs.
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
    {0x83880400, 0x82C21B88},  // tier0_360 #801
    {0x83880404, 0x82C21B68},  // tier0_360 #799
    {0x83880408, 0x82C21838},  // tier0_360 #788
    {0x8388040C, 0x82C11CB0},  // tier0_360 #90
    {0x83880410, 0x82C11CB0},  // tier0_360 #92
    {0x83880414, 0x82C11FE8},  // tier0_360 #108
    {0x83880418, 0x82C13A28},  // tier0_360 #185
    {0x8388041C, 0x82C10678},  // tier0_360 #41
    {0x83880420, 0x82C106F0},  // tier0_360 #42
    {0x83880424, 0x82C11C10},  // tier0_360 #88
    {0x83880428, 0x82C406CC},  // tier0_360 #234 (data)
    {0x8388042C, 0x82C19100},  // tier0_360 #506
    {0x83880430, 0x82C190B0},  // tier0_360 #509
    {0x83880434, 0x82C197A8},  // tier0_360 #565
    {0x83880438, 0x82C18B18},  // tier0_360 #475
    {0x8388043C, 0x82C10440},  // tier0_360 #39
    {0x83880440, 0x82C1A978},  // tier0_360 #585
    {0x83880444, 0x82C10B60},  // tier0_360 #45
    {0x83880448, 0x82C1A108},  // tier0_360 #568
    {0x8388044C, 0x82C19E18},  // tier0_360 #567
    {0x83880450, 0x82C11F48},  // tier0_360 #107
    {0x83880454, 0x82C12148},  // tier0_360 #110
    {0x83880458, 0x82C11E08},  // tier0_360 #105
    {0x8388045C, 0x82C11EA8},  // tier0_360 #106
    {0x83880464, 0x82E1BF50},  // vstdlib_360 #18
    {0x83880468, 0x82E1BF68},  // vstdlib_360 #19
    {0x8388046C, 0x82E1BF38},  // vstdlib_360 #17
    {0x83880470, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: soundemittersystem_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
