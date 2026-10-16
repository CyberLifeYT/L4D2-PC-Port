// matchmaking_360: 48 import slots (2 data, 46 function) from guest DLLs.
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
    {0x8B080400, 0x82C18B18},  // tier0_360 #475
    {0x8B080404, 0x82C11CB0},  // tier0_360 #92
    {0x8B080408, 0x82C197A8},  // tier0_360 #565
    {0x8B08040C, 0x82C19100},  // tier0_360 #506
    {0x8B080410, 0x82C190B0},  // tier0_360 #509
    {0x8B080414, 0x82C1AE20},  // tier0_360 #597
    {0x8B080418, 0x82C18B78},  // tier0_360 #479
    {0x8B08041C, 0x82C18DB8},  // tier0_360 #493
    {0x8B080420, 0x82C18E18},  // tier0_360 #495
    {0x8B080424, 0x82C18D68},  // tier0_360 #492
    {0x8B080428, 0x82C18D40},  // tier0_360 #488
    {0x8B08042C, 0x82C18B10},  // tier0_360 #474
    {0x8B080430, 0x82C21AB0},  // tier0_360 #795
    {0x8B080434, 0x82C21A18},  // tier0_360 #793
    {0x8B080438, 0x82C21A38},  // tier0_360 #794
    {0x8B08043C, 0x82C21BD8},  // tier0_360 #806
    {0x8B080440, 0x82C21C78},  // tier0_360 #810
    {0x8B080444, 0x82C21B58},  // tier0_360 #798
    {0x8B080448, 0x82C21B78},  // tier0_360 #800
    {0x8B08044C, 0x82C21D50},  // tier0_360 #813
    {0x8B080450, 0x82C21E70},  // tier0_360 #817
    {0x8B080454, 0x82C11F48},  // tier0_360 #107
    {0x8B080458, 0x82C11E08},  // tier0_360 #105
    {0x8B08045C, 0x82C11FE8},  // tier0_360 #108
    {0x8B080460, 0x82C19028},  // tier0_360 #505
    {0x8B080464, 0x82C19000},  // tier0_360 #504
    {0x8B080468, 0x82C10CD8},  // tier0_360 #48
    {0x8B08046C, 0x82C21838},  // tier0_360 #788
    {0x8B080470, 0x82C11CB0},  // tier0_360 #89
    {0x8B080474, 0x82C11CB0},  // tier0_360 #90
    {0x8B080478, 0x82C406CC},  // tier0_360 #234 (data)
    {0x8B08047C, 0x82C12148},  // tier0_360 #110
    {0x8B080480, 0x82C11EA8},  // tier0_360 #106
    {0x8B080484, 0x82C21D20},  // tier0_360 #812
    {0x8B080488, 0x82C21B88},  // tier0_360 #801
    {0x8B08048C, 0x82C21E40},  // tier0_360 #816
    {0x8B080490, 0x82C121E8},  // tier0_360 #111
    {0x8B080494, 0x82C18538},  // tier0_360 #292
    {0x8B080498, 0x82C21B98},  // tier0_360 #802
    {0x8B08049C, 0x82C21B48},  // tier0_360 #797
    {0x8B0804A0, 0x82C21BC8},  // tier0_360 #805
    {0x8B0804A4, 0x82C21B68},  // tier0_360 #799
    {0x8B0804A8, 0x82C21AE0},  // tier0_360 #796
    {0x8B0804AC, 0x82C21CF0},  // tier0_360 #811
    {0x8B0804B0, 0x82C12288},  // tier0_360 #112
    {0x8B0804B8, 0x82E302A0},  // vstdlib_360 #4 (data)
    {0x8B0804BC, 0x82E1B5E0},  // vstdlib_360 #13
    {0x8B0804C0, 0x82E1BF68},  // vstdlib_360 #19
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
    REXLOG_INFO("import fixup: matchmaking_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
