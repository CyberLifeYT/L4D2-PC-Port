// vstdlib_360: 55 import slots (4 data, 51 function) from guest DLLs.
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
    {0x82E00400, 0x82C194F0},  // tier0_360 #527
    {0x82E00404, 0x82C11CB0},  // tier0_360 #90
    {0x82E00408, 0x82C11CB0},  // tier0_360 #92
    {0x82E0040C, 0x82C12288},  // tier0_360 #112
    {0x82E00410, 0x82C18B28},  // tier0_360 #477
    {0x82E00414, 0x82C19438},  // tier0_360 #531
    {0x82E00418, 0x82C18C88},  // tier0_360 #483
    {0x82E0041C, 0x82C18B40},  // tier0_360 #478
    {0x82E00420, 0x82C1AE80},  // tier0_360 #598
    {0x82E00424, 0x82C19528},  // tier0_360 #540
    {0x82E00428, 0x82C1A5D0},  // tier0_360 #575
    {0x82E0042C, 0x82C19530},  // tier0_360 #541
    {0x82E00430, 0x82C19708},  // tier0_360 #545
    {0x82E00434, 0x82C11CB0},  // tier0_360 #526
    {0x82E00438, 0x82C182E0},  // tier0_360 #525
    {0x82E0043C, 0x82C1B3B0},  // tier0_360 #524
    {0x82E00440, 0x82C1B188},  // tier0_360 #600
    {0x82E00444, 0x82C1CC80},  // tier0_360 #653
    {0x82E00448, 0x82C1C7A0},  // tier0_360 #650
    {0x82E0044C, 0x82C12148},  // tier0_360 #110
    {0x82E00450, 0x82C18E18},  // tier0_360 #495
    {0x82E00454, 0x82C18DE8},  // tier0_360 #494
    {0x82E00458, 0x82C19778},  // tier0_360 #564
    {0x82E0045C, 0x82C11B70},  // tier0_360 #49
    {0x82E00460, 0x82C19430},  // tier0_360 #530
    {0x82E00464, 0x82C18B78},  // tier0_360 #479
    {0x82E00468, 0x82C1AAC8},  // tier0_360 #587
    {0x82E0046C, 0x82C406CC},  // tier0_360 #234 (data)
    {0x82E00470, 0x82C10CD8},  // tier0_360 #48
    {0x82E00474, 0x82C11F48},  // tier0_360 #107
    {0x82E00478, 0x82C11EA8},  // tier0_360 #106
    {0x82E0047C, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x82E00480, 0x82C12318},  // tier0_360 #113
    {0x82E00484, 0x82C446D8},  // tier0_360 #120 (data)
    {0x82E00488, 0x82C19100},  // tier0_360 #506
    {0x82E0048C, 0x82C19000},  // tier0_360 #504
    {0x82E00490, 0x82C190B0},  // tier0_360 #509
    {0x82E00494, 0x82C18B18},  // tier0_360 #475
    {0x82E00498, 0x82C197A8},  // tier0_360 #565
    {0x82E0049C, 0x82C18D68},  // tier0_360 #492
    {0x82E004A0, 0x82C18D08},  // tier0_360 #484
    {0x82E004A4, 0x82C434AC},  // tier0_360 #833 (data)
    {0x82E004A8, 0x82C18D40},  // tier0_360 #488
    {0x82E004AC, 0x82C18B10},  // tier0_360 #474
    {0x82E004B0, 0x82C18BB8},  // tier0_360 #480
    {0x82E004B4, 0x82C19028},  // tier0_360 #505
    {0x82E004B8, 0x82C19050},  // tier0_360 #507
    {0x82E004BC, 0x82C11E08},  // tier0_360 #105
    {0x82E004C0, 0x82C186E0},  // tier0_360 #297
    {0x82E004C4, 0x82C19078},  // tier0_360 #511
    {0x82E004C8, 0x82C19158},  // tier0_360 #514
    {0x82E004CC, 0x82C18DB8},  // tier0_360 #493
    {0x82E004D0, 0x82C194B8},  // tier0_360 #536
    {0x82E004D4, 0x82C19488},  // tier0_360 #535
    {0x82E004D8, 0x82C1A570},  // tier0_360 #573
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
    REXLOG_INFO("import fixup: vstdlib_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
