// launcher_360: 48 import slots (2 data, 46 function) from guest DLLs.
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
    {0x83680400, 0x82C22E58},  // tier0_360 #860
    {0x83680404, 0x82C21A18},  // tier0_360 #793
    {0x83680408, 0x82C22238},  // tier0_360 #828
    {0x8368040C, 0x82C21AE0},  // tier0_360 #796
    {0x83680410, 0x82C21B98},  // tier0_360 #802
    {0x83680414, 0x82C21B58},  // tier0_360 #798
    {0x83680418, 0x82C21C08},  // tier0_360 #809
    {0x8368041C, 0x82C21B78},  // tier0_360 #800
    {0x83680420, 0x82C12148},  // tier0_360 #110
    {0x83680424, 0x82C21B88},  // tier0_360 #801
    {0x83680428, 0x82C21AB0},  // tier0_360 #795
    {0x8368042C, 0x82C224E8},  // tier0_360 #836
    {0x83680430, 0x82C11D10},  // tier0_360 #94
    {0x83680434, 0x82C18538},  // tier0_360 #292
    {0x83680438, 0x82C21510},  // tier0_360 #784
    {0x8368043C, 0x82C11EA8},  // tier0_360 #106
    {0x83680440, 0x82C11F48},  // tier0_360 #107
    {0x83680444, 0x82C10CD8},  // tier0_360 #48
    {0x83680448, 0x82C11E08},  // tier0_360 #105
    {0x8368044C, 0x82C22F68},  // tier0_360 #862
    {0x83680450, 0x82C22DE0},  // tier0_360 #859
    {0x83680454, 0x82C406CC},  // tier0_360 #234 (data)
    {0x83680458, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x8368045C, 0x82C211C0},  // tier0_360 #747
    {0x83680460, 0x82C182E0},  // tier0_360 #399
    {0x83680464, 0x82C12F10},  // tier0_360 #168
    {0x83680468, 0x82C13930},  // tier0_360 #182
    {0x8368046C, 0x82C12EE8},  // tier0_360 #167
    {0x83680470, 0x82C18748},  // tier0_360 #298
    {0x83680474, 0x82C186E0},  // tier0_360 #297
    {0x83680478, 0x82C11CB0},  // tier0_360 #92
    {0x8368047C, 0x82C11CB0},  // tier0_360 #90
    {0x83680480, 0x82C19100},  // tier0_360 #506
    {0x83680484, 0x82C190B0},  // tier0_360 #509
    {0x83680488, 0x82C197A8},  // tier0_360 #565
    {0x8368048C, 0x82C18B18},  // tier0_360 #475
    {0x83680490, 0x82C18D08},  // tier0_360 #484
    {0x83680494, 0x82C18AE8},  // tier0_360 #473
    {0x83680498, 0x82C18B78},  // tier0_360 #479
    {0x8368049C, 0x82C1AE20},  // tier0_360 #597
    {0x836804A0, 0x82C11CB0},  // tier0_360 #438
    {0x836804A4, 0x82C18D20},  // tier0_360 #486
    {0x836804A8, 0x82C121E8},  // tier0_360 #111
    {0x836804AC, 0x82C21838},  // tier0_360 #788
    {0x836804B0, 0x82C21B68},  // tier0_360 #799
    {0x836804B4, 0x82C21E98},  // tier0_360 #818
    {0x836804BC, 0x82E1B5E0},  // vstdlib_360 #13
    {0x836804C0, 0x82E101C8},  // vstdlib_360 #2
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
    REXLOG_INFO("import fixup: launcher_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
