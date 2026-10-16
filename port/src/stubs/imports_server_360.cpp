// server_360: 60 import slots (4 data, 56 function) from guest DLLs.
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
    {0x89780400, 0x82C106F0},  // tier0_360 #42
    {0x89780404, 0x82C10678},  // tier0_360 #41
    {0x89780408, 0x82C11C10},  // tier0_360 #88
    {0x8978040C, 0x82C224E8},  // tier0_360 #836
    {0x89780410, 0x82C10B60},  // tier0_360 #45
    {0x89780414, 0x82C1A978},  // tier0_360 #585
    {0x89780418, 0x82C10440},  // tier0_360 #39
    {0x8978041C, 0x82C1A108},  // tier0_360 #568
    {0x89780420, 0x82C649BC},  // tier0_360 #604 (data)
    {0x89780424, 0x82C19E18},  // tier0_360 #567
    {0x89780428, 0x82C18FD0},  // tier0_360 #502
    {0x8978042C, 0x82C121E8},  // tier0_360 #111
    {0x89780430, 0x82C18B18},  // tier0_360 #475
    {0x89780434, 0x82C197A8},  // tier0_360 #565
    {0x89780438, 0x82C190B0},  // tier0_360 #509
    {0x8978043C, 0x82C19100},  // tier0_360 #506
    {0x89780440, 0x82C18538},  // tier0_360 #292
    {0x89780444, 0x82C186E0},  // tier0_360 #297
    {0x89780448, 0x82C12148},  // tier0_360 #110
    {0x8978044C, 0x82C12098},  // tier0_360 #109
    {0x89780450, 0x82C11FE8},  // tier0_360 #108
    {0x89780454, 0x82C11EA8},  // tier0_360 #106
    {0x89780458, 0x82C21B88},  // tier0_360 #801
    {0x8978045C, 0x82C11F48},  // tier0_360 #107
    {0x89780460, 0x82C11E08},  // tier0_360 #105
    {0x89780464, 0x82C21B68},  // tier0_360 #799
    {0x89780468, 0x82C21838},  // tier0_360 #788
    {0x8978046C, 0x82C11CB0},  // tier0_360 #90
    {0x89780470, 0x82C11CB0},  // tier0_360 #92
    {0x89780474, 0x82C18788},  // tier0_360 #302
    {0x89780478, 0x82C18818},  // tier0_360 #304
    {0x8978047C, 0x82C12288},  // tier0_360 #112
    {0x89780480, 0x82C182D8},  // tier0_360 #296
    {0x89780484, 0x82C18518},  // tier0_360 #290
    {0x89780488, 0x82C18528},  // tier0_360 #291
    {0x8978048C, 0x82C18DB8},  // tier0_360 #493
    {0x89780490, 0x82C19100},  // tier0_360 #510
    {0x89780494, 0x82C19050},  // tier0_360 #507
    {0x89780498, 0x82C19158},  // tier0_360 #514
    {0x8978049C, 0x82C19078},  // tier0_360 #511
    {0x897804A0, 0x82C11D10},  // tier0_360 #94
    {0x897804A4, 0x82C18B10},  // tier0_360 #474
    {0x897804A8, 0x82C10038},  // tier0_360 #12
    {0x897804AC, 0x82C19028},  // tier0_360 #505
    {0x897804B0, 0x82C19000},  // tier0_360 #504
    {0x897804B4, 0x82C18D68},  // tier0_360 #492
    {0x897804B8, 0x82C406CC},  // tier0_360 #234 (data)
    {0x897804BC, 0x82C21D20},  // tier0_360 #812
    {0x897804C0, 0x82C10CD8},  // tier0_360 #48
    {0x897804C4, 0x82C13A28},  // tier0_360 #185
    {0x897804CC, 0x82E1B5E0},  // vstdlib_360 #13
    {0x897804D0, 0x82E1BF18},  // vstdlib_360 #16
    {0x897804D4, 0x82E1BF38},  // vstdlib_360 #17
    {0x897804D8, 0x82E1C770},  // vstdlib_360 #31
    {0x897804DC, 0x82E302A0},  // vstdlib_360 #4 (data)
    {0x897804E0, 0x82E3212C},  // vstdlib_360 #3 (data)
    {0x897804E4, 0x82E1C338},  // vstdlib_360 #23
    {0x897804E8, 0x82E1BF88},  // vstdlib_360 #22
    {0x897804EC, 0x82E1C460},  // vstdlib_360 #24
    {0x897804F0, 0x82E1BF68},  // vstdlib_360 #19
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
    REXLOG_INFO("import fixup: server_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
