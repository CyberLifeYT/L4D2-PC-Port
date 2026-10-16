// vguimatsurface_360: 31 import slots (1 data, 30 function) from guest DLLs.
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
    {0x84E00400, 0x82C18B18},  // tier0_360 #475
    {0x84E00404, 0x82C197A8},  // tier0_360 #565
    {0x84E00408, 0x82C19100},  // tier0_360 #506
    {0x84E0040C, 0x82C18538},  // tier0_360 #292
    {0x84E00410, 0x82C21B88},  // tier0_360 #801
    {0x84E00414, 0x82C21B68},  // tier0_360 #799
    {0x84E00418, 0x82C186E0},  // tier0_360 #297
    {0x84E0041C, 0x82C12148},  // tier0_360 #110
    {0x84E00420, 0x82C406CC},  // tier0_360 #234 (data)
    {0x84E00424, 0x82C11F48},  // tier0_360 #107
    {0x84E00428, 0x82C11E08},  // tier0_360 #105
    {0x84E0042C, 0x82C22978},  // tier0_360 #848
    {0x84E00430, 0x82C22978},  // tier0_360 #849
    {0x84E00434, 0x82C22560},  // tier0_360 #838
    {0x84E00438, 0x82C22628},  // tier0_360 #841
    {0x84E0043C, 0x82C22998},  // tier0_360 #850
    {0x84E00440, 0x82C11EA8},  // tier0_360 #106
    {0x84E00444, 0x82C10CD8},  // tier0_360 #48
    {0x84E00448, 0x82C21838},  // tier0_360 #788
    {0x84E0044C, 0x82C11D10},  // tier0_360 #94
    {0x84E00450, 0x82C190B0},  // tier0_360 #509
    {0x84E00454, 0x82C11CB0},  // tier0_360 #92
    {0x84E00458, 0x82C11CB0},  // tier0_360 #90
    {0x84E0045C, 0x82C11CB0},  // tier0_360 #89
    {0x84E00460, 0x82C1A108},  // tier0_360 #568
    {0x84E00464, 0x82C19E18},  // tier0_360 #567
    {0x84E00468, 0x82C1A978},  // tier0_360 #585
    {0x84E0046C, 0x82C10B60},  // tier0_360 #45
    {0x84E00470, 0x82C10440},  // tier0_360 #39
    {0x84E00474, 0x82C11FE8},  // tier0_360 #108
    {0x84E0047C, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: vguimatsurface_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
