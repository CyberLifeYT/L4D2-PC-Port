// gameui_360: 61 import slots (1 data, 60 function) from guest DLLs.
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
    {0x84000400, 0x82C11FE8},  // tier0_360 #108
    {0x84000404, 0x82C19E18},  // tier0_360 #567
    {0x84000408, 0x82C1A108},  // tier0_360 #568
    {0x8400040C, 0x82C10B60},  // tier0_360 #45
    {0x84000410, 0x82C1A978},  // tier0_360 #585
    {0x84000414, 0x82C10440},  // tier0_360 #39
    {0x84000418, 0x82C11CB0},  // tier0_360 #89
    {0x8400041C, 0x82C11CB0},  // tier0_360 #90
    {0x84000420, 0x82C11CB0},  // tier0_360 #92
    {0x84000424, 0x82C22DE0},  // tier0_360 #859
    {0x84000428, 0x82C18D20},  // tier0_360 #486
    {0x8400042C, 0x82C11CB0},  // tier0_360 #438
    {0x84000430, 0x82C186E0},  // tier0_360 #297
    {0x84000434, 0x82C18D08},  // tier0_360 #484
    {0x84000438, 0x82C18B18},  // tier0_360 #475
    {0x8400043C, 0x82C197A8},  // tier0_360 #565
    {0x84000440, 0x82C19100},  // tier0_360 #506
    {0x84000444, 0x82C190B0},  // tier0_360 #509
    {0x84000448, 0x82C18BB8},  // tier0_360 #480
    {0x8400044C, 0x82C22278},  // tier0_360 #829
    {0x84000450, 0x82C218B8},  // tier0_360 #789
    {0x84000454, 0x82C21960},  // tier0_360 #791
    {0x84000458, 0x82C21C78},  // tier0_360 #810
    {0x8400045C, 0x82C21E70},  // tier0_360 #817
    {0x84000460, 0x82C21BE8},  // tier0_360 #807
    {0x84000464, 0x82C21BA8},  // tier0_360 #803
    {0x84000468, 0x82C21BD8},  // tier0_360 #806
    {0x8400046C, 0x82C1AE20},  // tier0_360 #597
    {0x84000470, 0x82C18B78},  // tier0_360 #479
    {0x84000474, 0x82C18AE8},  // tier0_360 #473
    {0x84000478, 0x82C21AB0},  // tier0_360 #795
    {0x8400047C, 0x82C21A18},  // tier0_360 #793
    {0x84000480, 0x82C21BC8},  // tier0_360 #805
    {0x84000484, 0x82C21D50},  // tier0_360 #813
    {0x84000488, 0x82C21DE8},  // tier0_360 #815
    {0x8400048C, 0x82C21C08},  // tier0_360 #809
    {0x84000490, 0x82C21B58},  // tier0_360 #798
    {0x84000494, 0x82C21B78},  // tier0_360 #800
    {0x84000498, 0x82C21B98},  // tier0_360 #802
    {0x8400049C, 0x82C21AE0},  // tier0_360 #796
    {0x840004A0, 0x82C21B68},  // tier0_360 #799
    {0x840004A4, 0x82C18B10},  // tier0_360 #474
    {0x840004A8, 0x82C21E40},  // tier0_360 #816
    {0x840004AC, 0x82C21B48},  // tier0_360 #797
    {0x840004B0, 0x82C21D20},  // tier0_360 #812
    {0x840004B4, 0x82C21E98},  // tier0_360 #818
    {0x840004B8, 0x82C21B88},  // tier0_360 #801
    {0x840004BC, 0x82C10CD8},  // tier0_360 #48
    {0x840004C0, 0x82C11D10},  // tier0_360 #94
    {0x840004C4, 0x82C21838},  // tier0_360 #788
    {0x840004C8, 0x82C121E8},  // tier0_360 #111
    {0x840004CC, 0x82C22E58},  // tier0_360 #860
    {0x840004D0, 0x82C12148},  // tier0_360 #110
    {0x840004D4, 0x82C11E08},  // tier0_360 #105
    {0x840004D8, 0x82C18538},  // tier0_360 #292
    {0x840004DC, 0x82C11EA8},  // tier0_360 #106
    {0x840004E0, 0x82C11F48},  // tier0_360 #107
    {0x840004E4, 0x82C406CC},  // tier0_360 #234 (data)
    {0x840004EC, 0x82E1BF68},  // vstdlib_360 #19
    {0x840004F0, 0x82E1BF38},  // vstdlib_360 #17
    {0x840004F4, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: gameui_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
