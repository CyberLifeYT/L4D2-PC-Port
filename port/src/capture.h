// L4D2 PC port - screenshots and timed diagnostics captures.
#pragma once

#include <string>

namespace rex::ui {
class Presenter;
}

namespace l4d2 {
// Starts the capture service. F12 saves logs\shots\shot_<time>.bmp; with
// l4d2_autoshot=N (seconds) a frame is also saved every N seconds, which is
// how rendering problems (movies, lighting) are compared between settings.
void StartCapture(rex::ui::Presenter* presenter);
void StopCapture();
// Saves the current guest frame. Returns the file written, or "" on failure.
std::string CaptureNow(const char* tag);
}  // namespace l4d2
