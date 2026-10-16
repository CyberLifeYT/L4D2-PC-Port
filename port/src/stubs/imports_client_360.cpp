// client_360: 80 import slots (6 data, 74 function) from guest DLLs.
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
    {0x88100400, 0x82C121E8},  // tier0_360 #111
    {0x88100404, 0x82C21A18},  // tier0_360 #793
    {0x88100408, 0x82C21A38},  // tier0_360 #794
    {0x8810040C, 0x82C12288},  // tier0_360 #112
    {0x88100410, 0x82C13A28},  // tier0_360 #185
    {0x88100414, 0x82C10678},  // tier0_360 #41
    {0x88100418, 0x82C106F0},  // tier0_360 #42
    {0x8810041C, 0x82C11C10},  // tier0_360 #88
    {0x88100420, 0x82C446D8},  // tier0_360 #120 (data)
    {0x88100424, 0x82C18DB8},  // tier0_360 #493
    {0x88100428, 0x82C197A8},  // tier0_360 #565
    {0x8810042C, 0x82C18B18},  // tier0_360 #475
    {0x88100430, 0x82C19100},  // tier0_360 #506
    {0x88100434, 0x82C19100},  // tier0_360 #510
    {0x88100438, 0x82C19050},  // tier0_360 #507
    {0x8810043C, 0x82C18BB8},  // tier0_360 #480
    {0x88100440, 0x82C10B60},  // tier0_360 #45
    {0x88100444, 0x82C1A978},  // tier0_360 #585
    {0x88100448, 0x82C10440},  // tier0_360 #39
    {0x8810044C, 0x82C19028},  // tier0_360 #505
    {0x88100450, 0x82C19000},  // tier0_360 #504
    {0x88100454, 0x82C190B0},  // tier0_360 #509
    {0x88100458, 0x82C19158},  // tier0_360 #514
    {0x8810045C, 0x82C19078},  // tier0_360 #511
    {0x88100460, 0x82C18538},  // tier0_360 #292
    {0x88100464, 0x82C186E0},  // tier0_360 #297
    {0x88100468, 0x82C12098},  // tier0_360 #109
    {0x8810046C, 0x82C11FE8},  // tier0_360 #108
    {0x88100470, 0x82C21CF0},  // tier0_360 #811
    {0x88100474, 0x82C21B88},  // tier0_360 #801
    {0x88100478, 0x82C21E40},  // tier0_360 #816
    {0x8810047C, 0x82C11F48},  // tier0_360 #107
    {0x88100480, 0x82C12148},  // tier0_360 #110
    {0x88100484, 0x82C21D20},  // tier0_360 #812
    {0x88100488, 0x82C11E08},  // tier0_360 #105
    {0x8810048C, 0x82C10CD8},  // tier0_360 #48
    {0x88100490, 0x82C11EA8},  // tier0_360 #106
    {0x88100494, 0x82C406CC},  // tier0_360 #234 (data)
    {0x88100498, 0x82C12318},  // tier0_360 #113
    {0x8810049C, 0x82C18D68},  // tier0_360 #492
    {0x881004A0, 0x82C10038},  // tier0_360 #12
    {0x881004A4, 0x82C18B10},  // tier0_360 #474
    {0x881004A8, 0x82C18FD0},  // tier0_360 #502
    {0x881004AC, 0x82C19E18},  // tier0_360 #567
    {0x881004B0, 0x82C649BC},  // tier0_360 #604 (data)
    {0x881004B4, 0x82C1A108},  // tier0_360 #568
    {0x881004B8, 0x82C11B70},  // tier0_360 #49
    {0x881004BC, 0x82C11D10},  // tier0_360 #94
    {0x881004C0, 0x82C123B8},  // tier0_360 #114
    {0x881004C4, 0x82C446F0},  // tier0_360 #117 (data)
    {0x881004C8, 0x82C21B68},  // tier0_360 #799
    {0x881004CC, 0x82C185D8},  // tier0_360 #293
    {0x881004D0, 0x82C190B0},  // tier0_360 #512
    {0x881004D4, 0x82C11CB0},  // tier0_360 #92
    {0x881004D8, 0x82C22DE0},  // tier0_360 #859
    {0x881004DC, 0x82C21838},  // tier0_360 #788
    {0x881004E0, 0x82C18D20},  // tier0_360 #486
    {0x881004E4, 0x82C11CB0},  // tier0_360 #438
    {0x881004E8, 0x82C1AE20},  // tier0_360 #597
    {0x881004EC, 0x82C18B78},  // tier0_360 #479
    {0x881004F0, 0x82C18AE8},  // tier0_360 #473
    {0x881004F4, 0x82C18D08},  // tier0_360 #484
    {0x881004F8, 0x82C11CB0},  // tier0_360 #89
    {0x881004FC, 0x82C11CB0},  // tier0_360 #90
    {0x88100500, 0x82C21AB0},  // tier0_360 #795
    {0x88100508, 0x82E1BF18},  // vstdlib_360 #16
    {0x8810050C, 0x82E3212C},  // vstdlib_360 #3 (data)
    {0x88100510, 0x82E302A0},  // vstdlib_360 #4 (data)
    {0x88100514, 0x82E1BF38},  // vstdlib_360 #17
    {0x88100518, 0x82E1C4E8},  // vstdlib_360 #28
    {0x8810051C, 0x82E1C7C0},  // vstdlib_360 #32
    {0x88100520, 0x82E1C770},  // vstdlib_360 #31
    {0x88100524, 0x82E1C5A0},  // vstdlib_360 #29
    {0x88100528, 0x82E18030},  // vstdlib_360 #7
    {0x8810052C, 0x82E1AD48},  // vstdlib_360 #12
    {0x88100530, 0x82E1C338},  // vstdlib_360 #23
    {0x88100534, 0x82E1BF88},  // vstdlib_360 #22
    {0x88100538, 0x82E1C460},  // vstdlib_360 #24
    {0x8810053C, 0x82E1B5E0},  // vstdlib_360 #13
    {0x88100540, 0x82E1BF68},  // vstdlib_360 #19
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
    REXLOG_INFO("import fixup: client_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
