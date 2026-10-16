// engine_360: 152 import slots (10 data, 142 function) from guest DLLs.
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
    {0x86B00400, 0x82C18F48},  // tier0_360 #500
    {0x86B00404, 0x82C13AA8},  // tier0_360 #186
    {0x86B00408, 0x82C13B80},  // tier0_360 #187
    {0x86B0040C, 0x82C21B68},  // tier0_360 #799
    {0x86B00410, 0x82C18D20},  // tier0_360 #486
    {0x86B00414, 0x82C11CB0},  // tier0_360 #438
    {0x86B00418, 0x82C1AE20},  // tier0_360 #597
    {0x86B0041C, 0x82C18D08},  // tier0_360 #484
    {0x86B00420, 0x82C11CB0},  // tier0_360 #89
    {0x86B00424, 0x82C11CB0},  // tier0_360 #90
    {0x86B00428, 0x82C11CB0},  // tier0_360 #92
    {0x86B0042C, 0x82C12EE8},  // tier0_360 #167
    {0x86B00430, 0x82C18C18},  // tier0_360 #482
    {0x86B00434, 0x82C18AE8},  // tier0_360 #473
    {0x86B00438, 0x82C13058},  // tier0_360 #173
    {0x86B0043C, 0x82C19100},  // tier0_360 #510
    {0x86B00440, 0x82C19050},  // tier0_360 #507
    {0x86B00444, 0x82C18788},  // tier0_360 #302
    {0x86B00448, 0x82C21BD8},  // tier0_360 #806
    {0x86B0044C, 0x82C21BF8},  // tier0_360 #808
    {0x86B00450, 0x82C21BB8},  // tier0_360 #804
    {0x86B00454, 0x82C131B0},  // tier0_360 #178
    {0x86B00458, 0x82C13090},  // tier0_360 #174
    {0x86B0045C, 0x82C130B8},  // tier0_360 #175
    {0x86B00460, 0x82C18AC8},  // tier0_360 #457
    {0x86B00464, 0x82C18698},  // tier0_360 #294
    {0x86B00468, 0x82C182D8},  // tier0_360 #296
    {0x86B0046C, 0x82C21AE0},  // tier0_360 #796
    {0x86B00470, 0x82C22F10},  // tier0_360 #861
    {0x86B00474, 0x82C12098},  // tier0_360 #109
    {0x86B00478, 0x82C18518},  // tier0_360 #290
    {0x86B0047C, 0x82C22838},  // tier0_360 #845
    {0x86B00480, 0x82C22998},  // tier0_360 #850
    {0x86B00484, 0x82C23190},  // tier0_360 #864
    {0x86B00488, 0x82C22560},  // tier0_360 #838
    {0x86B0048C, 0x82C22628},  // tier0_360 #841
    {0x86B00490, 0x82C226B0},  // tier0_360 #843
    {0x86B00494, 0x82C22978},  // tier0_360 #848
    {0x86B00498, 0x82C22C28},  // tier0_360 #854
    {0x86B0049C, 0x82C22C88},  // tier0_360 #855
    {0x86B004A0, 0x82C218B8},  // tier0_360 #789
    {0x86B004A4, 0x82C226A8},  // tier0_360 #842
    {0x86B004A8, 0x82C22278},  // tier0_360 #829
    {0x86B004AC, 0x82C224D8},  // tier0_360 #830
    {0x86B004B0, 0x82C18528},  // tier0_360 #291
    {0x86B004B4, 0x82C18AD0},  // tier0_360 #458
    {0x86B004B8, 0x82C21600},  // tier0_360 #786
    {0x86B004BC, 0x82C22CC0},  // tier0_360 #856
    {0x86B004C0, 0x82C18FE8},  // tier0_360 #503
    {0x86B004C4, 0x82C18748},  // tier0_360 #298
    {0x86B004C8, 0x82C12FB8},  // tier0_360 #171
    {0x86B004CC, 0x82C12F80},  // tier0_360 #170
    {0x86B004D0, 0x82C12F10},  // tier0_360 #168
    {0x86B004D4, 0x82C13248},  // tier0_360 #180
    {0x86B004D8, 0x82C18F90},  // tier0_360 #501
    {0x86B004DC, 0x82C186D8},  // tier0_360 #295
    {0x86B004E0, 0x82C10440},  // tier0_360 #39
    {0x86B004E4, 0x82C10B60},  // tier0_360 #45
    {0x86B004E8, 0x82C1A978},  // tier0_360 #585
    {0x86B004EC, 0x82C649BC},  // tier0_360 #604 (data)
    {0x86B004F0, 0x82C18FD0},  // tier0_360 #502
    {0x86B004F4, 0x82C1A108},  // tier0_360 #568
    {0x86B004F8, 0x82C19E18},  // tier0_360 #567
    {0x86B004FC, 0x82C11CB0},  // tier0_360 #520
    {0x86B00500, 0x82C1AE80},  // tier0_360 #598
    {0x86B00504, 0x82C19430},  // tier0_360 #530
    {0x86B00508, 0x82C19410},  // tier0_360 #529
    {0x86B0050C, 0x82C193C0},  // tier0_360 #528
    {0x86B00510, 0x82C194F0},  // tier0_360 #527
    {0x86B00514, 0x82C11CB0},  // tier0_360 #526
    {0x86B00518, 0x82C182E0},  // tier0_360 #525
    {0x86B0051C, 0x82C19358},  // tier0_360 #522
    {0x86B00520, 0x82C192C0},  // tier0_360 #519
    {0x86B00524, 0x82C185D8},  // tier0_360 #293
    {0x86B00528, 0x82C10138},  // tier0_360 #24
    {0x86B0052C, 0x82C10428},  // tier0_360 #23
    {0x86B00530, 0x82C1B3B0},  // tier0_360 #524
    {0x86B00534, 0x82C13A28},  // tier0_360 #185
    {0x86B00538, 0x82C10678},  // tier0_360 #41
    {0x86B0053C, 0x82C106F0},  // tier0_360 #42
    {0x86B00540, 0x82C11C10},  // tier0_360 #88
    {0x86B00544, 0x82C18D10},  // tier0_360 #485
    {0x86B00548, 0x82C18B10},  // tier0_360 #474
    {0x86B0054C, 0x82C18DE8},  // tier0_360 #494
    {0x86B00550, 0x82C18B78},  // tier0_360 #479
    {0x86B00554, 0x82C18D68},  // tier0_360 #492
    {0x86B00558, 0x82C18E18},  // tier0_360 #495
    {0x86B0055C, 0x82C18DB8},  // tier0_360 #493
    {0x86B00560, 0x82C18D40},  // tier0_360 #488
    {0x86B00564, 0x82C21CF0},  // tier0_360 #811
    {0x86B00568, 0x82C21B48},  // tier0_360 #797
    {0x86B0056C, 0x82C1CC80},  // tier0_360 #653
    {0x86B00570, 0x82C1C7A0},  // tier0_360 #650
    {0x86B00574, 0x82C446F0},  // tier0_360 #117 (data)
    {0x86B00578, 0x82C446E8},  // tier0_360 #118 (data)
    {0x86B0057C, 0x82C446E0},  // tier0_360 #119 (data)
    {0x86B00580, 0x82C446D0},  // tier0_360 #121 (data)
    {0x86B00584, 0x82C22F68},  // tier0_360 #862
    {0x86B00588, 0x82C22DE0},  // tier0_360 #859
    {0x86B0058C, 0x82C224E8},  // tier0_360 #836
    {0x86B00590, 0x82C446D8},  // tier0_360 #120 (data)
    {0x86B00594, 0x82C13930},  // tier0_360 #182
    {0x86B00598, 0x82C13978},  // tier0_360 #183
    {0x86B0059C, 0x82C21960},  // tier0_360 #791
    {0x86B005A0, 0x82C21E98},  // tier0_360 #818
    {0x86B005A4, 0x82C21838},  // tier0_360 #788
    {0x86B005A8, 0x82C21D20},  // tier0_360 #812
    {0x86B005AC, 0x82C21B88},  // tier0_360 #801
    {0x86B005B0, 0x82C21E40},  // tier0_360 #816
    {0x86B005B4, 0x82C21AB0},  // tier0_360 #795
    {0x86B005B8, 0x82C21A18},  // tier0_360 #793
    {0x86B005BC, 0x82C21A38},  // tier0_360 #794
    {0x86B005C0, 0x82C11FE8},  // tier0_360 #108
    {0x86B005C4, 0x82C18BB8},  // tier0_360 #480
    {0x86B005C8, 0x82C121E8},  // tier0_360 #111
    {0x86B005CC, 0x82C11B70},  // tier0_360 #49
    {0x86B005D0, 0x82C64C3C},  // tier0_360 #746 (data)
    {0x86B005D4, 0x82C186E0},  // tier0_360 #297
    {0x86B005D8, 0x82C19158},  // tier0_360 #514
    {0x86B005DC, 0x82C19078},  // tier0_360 #511
    {0x86B005E0, 0x82C19028},  // tier0_360 #505
    {0x86B005E4, 0x82C19000},  // tier0_360 #504
    {0x86B005E8, 0x82C22D28},  // tier0_360 #857
    {0x86B005EC, 0x82C22D80},  // tier0_360 #858
    {0x86B005F0, 0x82C1ADB0},  // tier0_360 #596
    {0x86B005F4, 0x82C18C88},  // tier0_360 #483
    {0x86B005F8, 0x82C12288},  // tier0_360 #112
    {0x86B005FC, 0x82C11D10},  // tier0_360 #94
    {0x86B00600, 0x82C12318},  // tier0_360 #113
    {0x86B00604, 0x82C10CD8},  // tier0_360 #48
    {0x86B00608, 0x82C123B8},  // tier0_360 #114
    {0x86B0060C, 0x82C12148},  // tier0_360 #110
    {0x86B00610, 0x82C11F48},  // tier0_360 #107
    {0x86B00614, 0x82C18538},  // tier0_360 #292
    {0x86B00618, 0x82C11E08},  // tier0_360 #105
    {0x86B0061C, 0x82C13150},  // tier0_360 #177
    {0x86B00620, 0x82C13290},  // tier0_360 #181
    {0x86B00624, 0x82C11EA8},  // tier0_360 #106
    {0x86B00628, 0x82C18B18},  // tier0_360 #475
    {0x86B0062C, 0x82C197A8},  // tier0_360 #565
    {0x86B00630, 0x82C190B0},  // tier0_360 #509
    {0x86B00634, 0x82C19100},  // tier0_360 #506
    {0x86B00638, 0x82C406CC},  // tier0_360 #234 (data)
    {0x86B00640, 0x82E1BF50},  // vstdlib_360 #18
    {0x86B00644, 0x82E1BF38},  // vstdlib_360 #17
    {0x86B00648, 0x82E1AD48},  // vstdlib_360 #12
    {0x86B0064C, 0x82E3212C},  // vstdlib_360 #3 (data)
    {0x86B00650, 0x82E1BF18},  // vstdlib_360 #16
    {0x86B00654, 0x82E302A0},  // vstdlib_360 #4 (data)
    {0x86B00658, 0x82E1AA20},  // vstdlib_360 #11
    {0x86B0065C, 0x82E1BF68},  // vstdlib_360 #19
    {0x86B00660, 0x82E1B5E0},  // vstdlib_360 #13
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
    REXLOG_INFO("import fixup: engine_360 patched {} slots", std::size(kSlots));
  }
  __imp__xstart(ctx, base);
}
