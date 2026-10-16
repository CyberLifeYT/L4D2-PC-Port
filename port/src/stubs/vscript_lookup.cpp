// vscript_360 dispatch-table workaround (see fix_vscript_table.cmake).
//
// ReXGlue places each module's dispatch table at IMAGE_BASE+IMAGE_SIZE.
// vscript's full table would overlap materialsystem_360's image at 0x84900000,
// so vscript registers a narrower code window [0x84823AF8, 0x8485BAF8) with the
// runtime. Functions below the window are still compiled and directly
// callable; indirect calls to them (from any module) resolve through this
// export, which searches vscript's complete function list.
#include "l4d2_pch.h"

extern "C" __declspec(dllexport) PPCFunc* L4d2VscriptLookup(uint32_t guest_address) {
  static size_t count = [] {
    size_t n = 0;
    while (PPCFuncMappings[n].host) ++n;
    return n;
  }();
  size_t lo = 0, hi = count;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    size_t g = PPCFuncMappings[mid].guest;
    if (g == guest_address) return PPCFuncMappings[mid].host;
    if (g < guest_address) lo = mid + 1; else hi = mid;
  }
  return nullptr;
}
