// filesystem_stdio_360: 72 import slots (3 data, 69 function) from guest DLLs.
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
    {0x83000400, 0x82C18D68},  // tier0_360 #492
    {0x83000404, 0x82C19000},  // tier0_360 #504
    {0x83000408, 0x82C19028},  // tier0_360 #505
    {0x8300040C, 0x82C190B0},  // tier0_360 #509
    {0x83000410, 0x82C197A8},  // tier0_360 #565
    {0x83000414, 0x82C18B18},  // tier0_360 #475
    {0x83000418, 0x82C406CC},  // tier0_360 #234 (data)
    {0x8300041C, 0x82C10440},  // tier0_360 #39
    {0x83000420, 0x82C1A978},  // tier0_360 #585
    {0x83000424, 0x82C10B60},  // tier0_360 #45
    {0x83000428, 0x82C1A108},  // tier0_360 #568
    {0x8300042C, 0x82C19E18},  // tier0_360 #567
    {0x83000430, 0x82C11E08},  // tier0_360 #105
    {0x83000434, 0x82C18BB8},  // tier0_360 #480
    {0x83000438, 0x82C18F90},  // tier0_360 #501
    {0x8300043C, 0x82C22DE0},  // tier0_360 #859
    {0x83000440, 0x82C18748},  // tier0_360 #298
    {0x83000444, 0x82C19050},  // tier0_360 #507
    {0x83000448, 0x82C10428},  // tier0_360 #23
    {0x8300044C, 0x82C10138},  // tier0_360 #24
    {0x83000450, 0x82C18F48},  // tier0_360 #500
    {0x83000454, 0x82C18FD0},  // tier0_360 #502
    {0x83000458, 0x82C18FE8},  // tier0_360 #503
    {0x8300045C, 0x82C18B10},  // tier0_360 #539
    {0x83000460, 0x82C18538},  // tier0_360 #292
    {0x83000464, 0x82C11C10},  // tier0_360 #88
    {0x83000468, 0x82C106F0},  // tier0_360 #42
    {0x8300046C, 0x82C10678},  // tier0_360 #41
    {0x83000470, 0x82C13A28},  // tier0_360 #185
    {0x83000474, 0x82C11EA8},  // tier0_360 #106
    {0x83000478, 0x82C21B88},  // tier0_360 #801
    {0x8300047C, 0x82C21B68},  // tier0_360 #799
    {0x83000480, 0x82C18D20},  // tier0_360 #486
    {0x83000484, 0x82C11CB0},  // tier0_360 #438
    {0x83000488, 0x82C1AE20},  // tier0_360 #597
    {0x8300048C, 0x82C18B78},  // tier0_360 #479
    {0x83000490, 0x82C18AE8},  // tier0_360 #473
    {0x83000494, 0x82C18D08},  // tier0_360 #484
    {0x83000498, 0x82C11CB0},  // tier0_360 #89
    {0x8300049C, 0x82C11CB0},  // tier0_360 #90
    {0x830004A0, 0x82C11CB0},  // tier0_360 #92
    {0x830004A4, 0x82C218B8},  // tier0_360 #789
    {0x830004A8, 0x82C11D10},  // tier0_360 #94
    {0x830004AC, 0x82C185D8},  // tier0_360 #293
    {0x830004B0, 0x82C18DE8},  // tier0_360 #494
    {0x830004B4, 0x82C186E0},  // tier0_360 #297
    {0x830004B8, 0x82C1A410},  // tier0_360 #570
    {0x830004BC, 0x82C19100},  // tier0_360 #506
    {0x830004C0, 0x82C18B10},  // tier0_360 #474
    {0x830004C4, 0x82C18DB8},  // tier0_360 #493
    {0x830004C8, 0x82C18D40},  // tier0_360 #488
    {0x830004CC, 0x82C1AE80},  // tier0_360 #598
    {0x830004D0, 0x82C194F0},  // tier0_360 #527
    {0x830004D4, 0x82C11CB0},  // tier0_360 #526
    {0x830004D8, 0x82C182E0},  // tier0_360 #525
    {0x830004DC, 0x82C1B3B0},  // tier0_360 #524
    {0x830004E0, 0x82C19358},  // tier0_360 #522
    {0x830004E4, 0x82C11CB0},  // tier0_360 #520
    {0x830004E8, 0x82C192C0},  // tier0_360 #519
    {0x830004EC, 0x82C21960},  // tier0_360 #791
    {0x830004F0, 0x82C21838},  // tier0_360 #788
    {0x830004F4, 0x82C12148},  // tier0_360 #110
    {0x830004F8, 0x82C121E8},  // tier0_360 #111
    {0x830004FC, 0x82C10CD8},  // tier0_360 #48
    {0x83000500, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x83000504, 0x82C22D28},  // tier0_360 #857
    {0x83000508, 0x82C22D80},  // tier0_360 #858
    {0x8300050C, 0x82C11F48},  // tier0_360 #107
    {0x83000514, 0x82E1BF68},  // vstdlib_360 #19
    {0x83000518, 0x82E1AD48},  // vstdlib_360 #12
    {0x8300051C, 0x82E302A0},  // vstdlib_360 #4 (data)
    {0x83000520, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: filesystem_stdio_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
