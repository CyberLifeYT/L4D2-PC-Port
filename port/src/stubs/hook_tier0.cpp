// tier0 overrides.
#include "l4d2_pch.h"

#include <rex/hook.h>

// sub_82C19750: per-thread FPU/VMX mode setup (guest ThreadProc prologue).
//   mtfsfi 7,0   -> FPSCR rounding = round-to-nearest
//   lvx128/stvx128 of a constant, then mtvscr v0 -> VSCR (NJ/saturation)
// The recompiler has no mtfsfi/mtvscr, so the generated body throws
// "Unimplemented PPC instruction" and kills every tier0-created thread.
// Host defaults already match (round-to-nearest), so this is a no-op.
REX_HOOK_RAW(sub_82C19750) {
  (void)ctx;
  (void)base;
}
