// vgui2_360: 26 import slots (1 data, 25 function) from guest DLLs.
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
    {0x83C00400, 0x82C21B88},  // tier0_360 #801
    {0x83C00404, 0x82C21B68},  // tier0_360 #799
    {0x83C00408, 0x82C10440},  // tier0_360 #39
    {0x83C0040C, 0x82C10B60},  // tier0_360 #45
    {0x83C00410, 0x82C1A978},  // tier0_360 #585
    {0x83C00414, 0x82C19E18},  // tier0_360 #567
    {0x83C00418, 0x82C1A108},  // tier0_360 #568
    {0x83C0041C, 0x82C11FE8},  // tier0_360 #108
    {0x83C00420, 0x82C18B18},  // tier0_360 #475
    {0x83C00424, 0x82C197A8},  // tier0_360 #565
    {0x83C00428, 0x82C190B0},  // tier0_360 #509
    {0x83C0042C, 0x82C19100},  // tier0_360 #506
    {0x83C00430, 0x82C11CB0},  // tier0_360 #90
    {0x83C00434, 0x82C11CB0},  // tier0_360 #92
    {0x83C00438, 0x82C18748},  // tier0_360 #298
    {0x83C0043C, 0x82C11D10},  // tier0_360 #94
    {0x83C00440, 0x82C21838},  // tier0_360 #788
    {0x83C00444, 0x82C12148},  // tier0_360 #110
    {0x83C00448, 0x82C21770},  // tier0_360 #787
    {0x83C0044C, 0x82C18538},  // tier0_360 #292
    {0x83C00450, 0x82C11EA8},  // tier0_360 #106
    {0x83C00454, 0x82C406CC},  // tier0_360 #234 (data)
    {0x83C00458, 0x82C11E08},  // tier0_360 #105
    {0x83C0045C, 0x82C10CD8},  // tier0_360 #48
    {0x83C00460, 0x82C11F48},  // tier0_360 #107
    {0x83C00468, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: vgui2_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
