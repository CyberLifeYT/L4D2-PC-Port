// L4D2 PC port - guest frame counter (game FPS) from the Direct3D 12 backend.
#include "overlay.h"

#include <atomic>

#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/system/interfaces/graphics.h>

namespace l4d2 {
namespace {
std::atomic<rex::graphics::d3d12::D3D12CommandProcessor*> g_cp{nullptr};
}

void SetGraphicsSystem(rex::system::IGraphicsSystem* gs) {
  if (!gs) return;
  // The xenos plugin's graphics system derives from rex::graphics::GraphicsSystem.
  auto* system = static_cast<rex::graphics::GraphicsSystem*>(gs);
  if (system->name().find("12") == std::string::npos) return;  // Direct3D 12 only
  g_cp = static_cast<rex::graphics::d3d12::D3D12CommandProcessor*>(system->command_processor());
}

uint64_t GuestFrameCount() {
  auto* cp = g_cp.load(std::memory_order_relaxed);
  return cp ? cp->GetCurrentFrame() : 0;
}
}  // namespace l4d2
