// shaderapidx9_360: 48 import slots (4 data, 44 function) from guest DLLs.
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
    {0x85200400, 0x82C19100},  // tier0_360 #506
    {0x85200404, 0x82C446D0},  // tier0_360 #121 (data)
    {0x85200408, 0x82C190B0},  // tier0_360 #509
    {0x8520040C, 0x82C197A8},  // tier0_360 #565
    {0x85200410, 0x82C18B18},  // tier0_360 #475
    {0x85200414, 0x82C406CC},  // tier0_360 #234 (data)
    {0x85200418, 0x82C11E08},  // tier0_360 #105
    {0x8520041C, 0x82C12318},  // tier0_360 #113
    {0x85200420, 0x82C13AA8},  // tier0_360 #186
    {0x85200424, 0x82C13B80},  // tier0_360 #187
    {0x85200428, 0x82C21B88},  // tier0_360 #801
    {0x8520042C, 0x82C21B68},  // tier0_360 #799
    {0x85200430, 0x82C21838},  // tier0_360 #788
    {0x85200434, 0x82C10440},  // tier0_360 #39
    {0x85200438, 0x82C10B60},  // tier0_360 #45
    {0x8520043C, 0x82C1A978},  // tier0_360 #585
    {0x85200440, 0x82C19E18},  // tier0_360 #567
    {0x85200444, 0x82C1A108},  // tier0_360 #568
    {0x85200448, 0x82C11CB0},  // tier0_360 #90
    {0x8520044C, 0x82C11CB0},  // tier0_360 #92
    {0x85200450, 0x82C13A28},  // tier0_360 #185
    {0x85200454, 0x82C10678},  // tier0_360 #41
    {0x85200458, 0x82C106F0},  // tier0_360 #42
    {0x8520045C, 0x82C11C10},  // tier0_360 #88
    {0x85200460, 0x82C18D68},  // tier0_360 #492
    {0x85200464, 0x82C18D40},  // tier0_360 #488
    {0x85200468, 0x82C13930},  // tier0_360 #182
    {0x8520046C, 0x82C121E8},  // tier0_360 #111
    {0x85200470, 0x82C18BB8},  // tier0_360 #480
    {0x85200474, 0x82C11FE8},  // tier0_360 #108
    {0x85200478, 0x82C22998},  // tier0_360 #850
    {0x8520047C, 0x82C218F0},  // tier0_360 #790
    {0x85200480, 0x82C12148},  // tier0_360 #110
    {0x85200484, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x85200488, 0x82C22C28},  // tier0_360 #854
    {0x8520048C, 0x82C21E98},  // tier0_360 #818
    {0x85200490, 0x82C18AD0},  // tier0_360 #458
    {0x85200494, 0x82C11F48},  // tier0_360 #107
    {0x85200498, 0x82C18538},  // tier0_360 #292
    {0x8520049C, 0x82C19028},  // tier0_360 #505
    {0x852004A0, 0x82C19000},  // tier0_360 #504
    {0x852004A4, 0x82C19050},  // tier0_360 #507
    {0x852004A8, 0x82C18748},  // tier0_360 #298
    {0x852004AC, 0x82C10CD8},  // tier0_360 #48
    {0x852004B0, 0x82C186E0},  // tier0_360 #297
    {0x852004B4, 0x82C11EA8},  // tier0_360 #106
    {0x852004BC, 0x82E1B5E0},  // vstdlib_360 #13
    {0x852004C0, 0x82E302A0},  // vstdlib_360 #4 (data)
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
    REXLOG_INFO("import fixup: shaderapidx9_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
