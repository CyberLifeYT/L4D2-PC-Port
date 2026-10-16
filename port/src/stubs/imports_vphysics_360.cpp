// vphysics_360: 26 import slots (1 data, 25 function) from guest DLLs.
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
    {0x82200400, 0x82C13930},  // tier0_360 #182
    {0x82200404, 0x82C139E8},  // tier0_360 #184
    {0x82200408, 0x82C13150},  // tier0_360 #177
    {0x8220040C, 0x82C11CB0},  // tier0_360 #90
    {0x82200410, 0x82C11CB0},  // tier0_360 #92
    {0x82200414, 0x82C10CD8},  // tier0_360 #48
    {0x82200418, 0x82C197A8},  // tier0_360 #565
    {0x8220041C, 0x82C18B18},  // tier0_360 #475
    {0x82200420, 0x82C190B0},  // tier0_360 #509
    {0x82200424, 0x82C19100},  // tier0_360 #506
    {0x82200428, 0x82C18FE8},  // tier0_360 #503
    {0x8220042C, 0x82C406CC},  // tier0_360 #234 (data)
    {0x82200430, 0x82C11EA8},  // tier0_360 #106
    {0x82200434, 0x82C12148},  // tier0_360 #110
    {0x82200438, 0x82C11E08},  // tier0_360 #105
    {0x8220043C, 0x82C11FE8},  // tier0_360 #108
    {0x82200440, 0x82C11F48},  // tier0_360 #107
    {0x82200444, 0x82C10440},  // tier0_360 #39
    {0x82200448, 0x82C1A978},  // tier0_360 #585
    {0x8220044C, 0x82C10B60},  // tier0_360 #45
    {0x82200450, 0x82C1A108},  // tier0_360 #568
    {0x82200454, 0x82C19E18},  // tier0_360 #567
    {0x82200458, 0x82C186E0},  // tier0_360 #297
    {0x8220045C, 0x82C18F90},  // tier0_360 #501
    {0x82200460, 0x82C18F48},  // tier0_360 #500
    {0x82200464, 0x82C18FD0},  // tier0_360 #502
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
    REXLOG_INFO("import fixup: vphysics_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
