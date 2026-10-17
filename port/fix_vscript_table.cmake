# L4D2 port: vscript_360 dispatch-table workaround + indirect-call cache. Run after every codegen
# (codegen rewrites generated headers). Idempotent.
#   cmake -DPROJ=<rexproject dir> -P fix_vscript_table.cmake
#
# ReXGlue puts a module's dispatch table at IMAGE_BASE+IMAGE_SIZE, sized
# (code_size + 0x10000) * 2. vscript (image 0x84800000-0x84870000, code
# 0x84810000-0x8485BAF8) would need a table reaching 0x849275F0, overlapping
# materialsystem_360 at 0x84900000; the runtime then refuses to load vscript.
#
# Fix: register vscript with the runtime using a narrower code window
# [0x84823AF8, 0x8485BAF8) (table ends exactly at 0x84900000). Every module's
# indirect-call fallback first asks vscript (L4d2VscriptLookup, see
# src/stubs/vscript_lookup.cpp) for any address in vscript's code range.

set(_marker_v1 "L4D2 port: vscript dispatch fallback (inserted")
set(_marker "L4D2 port: indirect-call cache v2")
set(_resolve "rex_indirect_func_ = rex::runtime::ResolveIndirectFunction(rex_indirect_target_);")
set(_resolve_new "rex_indirect_func_ = L4d2ResolveIndirect(rex_indirect_target_);")
set(_anchor "#define REX_CALL_INDIRECT_FUNC(x)")

file(GLOB _pchs "${PROJ}/generated/*/l4d2_pch.h")
set(_count 0)
foreach(_f IN LISTS _pchs)
  get_filename_component(_dir "${_f}" DIRECTORY)
  get_filename_component(_mod "${_dir}" NAME)
  if(_mod STREQUAL "default")
    continue()
  endif()
  file(READ "${_f}" _t)
  set(_n "${_t}")
  if(_mod STREQUAL "vscript_360")
    # Undo an earlier (rejected) image-base relocation attempt, if present.
    string(REPLACE "#define REX_IMAGE_BASE 0x8C000000ull" "#define REX_IMAGE_BASE 0x84800000ull" _n "${_n}")
    string(REPLACE "#define REX_IMAGE_SIZE 0x10000ull" "#define REX_IMAGE_SIZE 0x70000ull" _n "${_n}")
    string(REPLACE "#define REX_CODE_BASE 0x84810000ull" "#define REX_CODE_BASE 0x84823AF8ull" _n "${_n}")
    string(REPLACE "#define REX_CODE_SIZE 0x4BAF8ull" "#define REX_CODE_SIZE 0x38000ull" _n "${_n}")
    set(_attr "__declspec(dllexport)")
  else()
    set(_attr "__declspec(dllimport)")
  endif()
  # Remove the v1 helper (a block ending at the REX_CALL_INDIRECT_FUNC anchor).
  string(FIND "${_n}" "// --- ${_marker_v1}" _v1)
  if(NOT _v1 EQUAL -1)
    string(FIND "${_n}" "${_anchor}" _a)
    math(EXPR _len "${_a} - ${_v1}")
    string(SUBSTRING "${_n}" ${_v1} ${_len} _old)
    string(REPLACE "${_old}" "" _n "${_n}")
  endif()
  string(FIND "${_n}" "${_marker}" _has)
  if(_has EQUAL -1)
    # Cross-module indirect calls (virtual calls into another DLL - constant in
    # Source) miss the caller's own dispatch table and go to the runtime's
    # ResolveIndirectFunction, which takes a mutex on every call; the main and
    # render threads then contend on it. A guest address always resolves to the
    # same host function, so each thread keeps a small lock-free cache.
    set(_helper "// --- ${_marker} (inserted by fix_vscript_table.cmake) ---
extern \"C\" ${_attr} PPCFunc* L4d2VscriptLookup(uint32_t guest_address);
static inline PPCFunc* L4d2ResolveIndirectSlow(uint32_t t) {
  if (t - 0x84810000u < 0x4BAF8u) {
    if (PPCFunc* f = L4d2VscriptLookup(t)) return f;
  }
  return rex::runtime::ResolveIndirectFunction(t);
}
struct L4d2IndirectSlot { uint32_t key; PPCFunc* fn; };
static inline PPCFunc* L4d2ResolveIndirect(uint32_t t) {
  static thread_local L4d2IndirectSlot cache[1024];
  L4d2IndirectSlot& s = cache[(t >> 2) & 1023u];
  if (s.key == t && s.fn) return s.fn;
  PPCFunc* f = L4d2ResolveIndirectSlow(t);
  if (f) { s.key = t; s.fn = f; }
  return f;
}

")
    string(REPLACE "${_anchor}" "${_helper}${_anchor}" _n "${_n}")
    string(REPLACE "${_resolve}" "${_resolve_new}" _n "${_n}")
  endif()
  if(NOT _n STREQUAL _t)
    file(WRITE "${_f}" "${_n}")
    math(EXPR _count "${_count}+1")
  endif()
endforeach()
message(STATUS "fix_vscript_table: patched ${_count} header(s)")
