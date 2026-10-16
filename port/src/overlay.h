// L4D2 PC port - in-game performance overlay (F2).
#pragma once

#include <memory>

namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
}  // namespace rex::ui

namespace rex::system {
class IGraphicsSystem;
}

#include <cstdint>

namespace l4d2 {
// Guest (game) frame counter from the GPU backend; 0 if unavailable.
void SetGraphicsSystem(rex::system::IGraphicsSystem* gs);
uint64_t GuestFrameCount();
// Quiets the audio log spam and hooks the "gpu" log category to count shader
// pipeline compiles and read the adapter name. Call after logging init and
// again after any log_level change.
void ApplyLogLevels();
// Creates the overlay dialog (always alive; draws only when enabled).
std::unique_ptr<rex::ui::ImGuiDialog> CreatePerfOverlay(rex::ui::ImGuiDrawer* drawer);
void TogglePerfOverlay();
}  // namespace l4d2
