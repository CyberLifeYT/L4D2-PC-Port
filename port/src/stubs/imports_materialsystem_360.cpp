// materialsystem_360: 60 import slots (3 data, 57 function) from guest DLLs.
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
    {0x84900400, 0x82C13A28},  // tier0_360 #185
    {0x84900404, 0x82C10678},  // tier0_360 #41
    {0x84900408, 0x82C106F0},  // tier0_360 #42
    {0x8490040C, 0x82C11C10},  // tier0_360 #88
    {0x84900410, 0x82C12318},  // tier0_360 #113
    {0x84900414, 0x82C19050},  // tier0_360 #507
    {0x84900418, 0x82C1AE20},  // tier0_360 #597
    {0x8490041C, 0x82C18B40},  // tier0_360 #478
    {0x84900420, 0x82C21B48},  // tier0_360 #797
    {0x84900424, 0x82C19E18},  // tier0_360 #567
    {0x84900428, 0x82C1A108},  // tier0_360 #568
    {0x8490042C, 0x82C10B60},  // tier0_360 #45
    {0x84900430, 0x82C1A978},  // tier0_360 #585
    {0x84900434, 0x82C10440},  // tier0_360 #39
    {0x84900438, 0x82C13930},  // tier0_360 #182
    {0x8490043C, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x84900440, 0x82C11B70},  // tier0_360 #49
    {0x84900444, 0x82C12148},  // tier0_360 #110
    {0x84900448, 0x82C18BB8},  // tier0_360 #480
    {0x8490044C, 0x82C10038},  // tier0_360 #12
    {0x84900450, 0x82C18D68},  // tier0_360 #492
    {0x84900454, 0x82C13150},  // tier0_360 #177
    {0x84900458, 0x82C13290},  // tier0_360 #181
    {0x8490045C, 0x82C22998},  // tier0_360 #850
    {0x84900460, 0x82C18B18},  // tier0_360 #475
    {0x84900464, 0x82C197A8},  // tier0_360 #565
    {0x84900468, 0x82C13AA8},  // tier0_360 #186
    {0x8490046C, 0x82C13B80},  // tier0_360 #187
    {0x84900470, 0x82C21B88},  // tier0_360 #801
    {0x84900474, 0x82C21B68},  // tier0_360 #799
    {0x84900478, 0x82C21838},  // tier0_360 #788
    {0x8490047C, 0x82C18D20},  // tier0_360 #486
    {0x84900480, 0x82C11CB0},  // tier0_360 #438
    {0x84900484, 0x82C18B78},  // tier0_360 #479
    {0x84900488, 0x82C18AE8},  // tier0_360 #473
    {0x8490048C, 0x82C18D08},  // tier0_360 #484
    {0x84900490, 0x82C11CB0},  // tier0_360 #90
    {0x84900494, 0x82C22DE0},  // tier0_360 #859
    {0x84900498, 0x82C11CB0},  // tier0_360 #92
    {0x8490049C, 0x82C218F0},  // tier0_360 #790
    {0x849004A0, 0x82C406CC},  // tier0_360 #234 (data)
    {0x849004A4, 0x82C11E08},  // tier0_360 #105
    {0x849004A8, 0x82C11EA8},  // tier0_360 #106
    {0x849004AC, 0x82C18538},  // tier0_360 #292
    {0x849004B0, 0x82C19000},  // tier0_360 #504
    {0x849004B4, 0x82C19028},  // tier0_360 #505
    {0x849004B8, 0x82C19100},  // tier0_360 #506
    {0x849004BC, 0x82C11F48},  // tier0_360 #107
    {0x849004C0, 0x82C12098},  // tier0_360 #109
    {0x849004C4, 0x82C12FF0},  // tier0_360 #172
    {0x849004C8, 0x82C18F90},  // tier0_360 #501
    {0x849004CC, 0x82C121E8},  // tier0_360 #111
    {0x849004D0, 0x82C10CD8},  // tier0_360 #48
    {0x849004D4, 0x82C186E0},  // tier0_360 #297
    {0x849004D8, 0x82C18F48},  // tier0_360 #500
    {0x849004DC, 0x82C18FD0},  // tier0_360 #502
    {0x849004E0, 0x82C18FE8},  // tier0_360 #503
    {0x849004E4, 0x82C190B0},  // tier0_360 #509
    {0x849004EC, 0x82E302A0},  // vstdlib_360 #4 (data)
    {0x849004F0, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: materialsystem_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
