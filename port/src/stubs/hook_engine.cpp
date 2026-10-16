// engine overrides.
#include "l4d2_pch.h"

#include <rex/hook.h>

// sub_86C820B8: engine's statically-linked copy of the per-thread FPU/VMX mode
// setup (same code as tier0 sub_82C19750): mtfsfi 7,0 + mtvscr. The
// recompiler has no mtfsfi/mtvscr and the generated body throws. Host defaults
// already match (round-to-nearest), so this is a no-op.
REX_HOOK_RAW(sub_86C820B8) {
  (void)ctx;
  (void)base;
}
