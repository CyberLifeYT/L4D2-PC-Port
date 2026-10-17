// L4D2 PC port - screenshots and timed diagnostics captures.
#include "capture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_INT32(l4d2_autoshot, 0, "L4D2 Debug",
                     "Save a screenshot to logs\\shots every N seconds (0 = off; F12 always works)");
REXCVAR_DEFINE_INT32(l4d2_autoshot_limit, 200, "L4D2 Debug", "Maximum automatic screenshots per session");
REXCVAR_DEFINE_STRING(l4d2_burst, "", "L4D2 Debug",
                      "Burst screenshots for GIFs: 'start_seconds:count:interval_ms' (files logs\\shots\\burst_*.bmp)");

namespace l4d2 {
namespace {

rex::ui::Presenter* g_presenter = nullptr;
std::thread g_thread;
std::mutex g_mutex;
std::condition_variable g_cv;
bool g_stop = false;
std::atomic<int> g_auto_count{0};
const auto g_start = std::chrono::steady_clock::now();

std::filesystem::path ShotDir() {
  auto dir = rex::filesystem::GetExecutableFolder() / "logs" / "shots";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

bool WriteBmp(const std::filesystem::path& path, const rex::ui::RawImage& img) {
  const uint32_t w = img.width, h = img.height;
  const uint32_t row = (w * 3 + 3) & ~3u;
  const uint32_t data_size = row * h;
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
  auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
  f.write("BM", 2);
  u32(54 + data_size); u32(0); u32(54);
  u32(40); u32(w); u32(h); u16(1); u16(24); u32(0); u32(data_size);
  u32(2835); u32(2835); u32(0); u32(0);
  std::vector<uint8_t> line(row, 0);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* src = img.data.data() + size_t(h - 1 - y) * img.stride;  // bottom-up
    for (uint32_t x = 0; x < w; ++x) {
      line[x * 3 + 0] = src[x * 4 + 2];  // B
      line[x * 3 + 1] = src[x * 4 + 1];  // G
      line[x * 3 + 2] = src[x * 4 + 0];  // R
    }
    f.write(reinterpret_cast<const char*>(line.data()), row);
  }
  return bool(f);
}

}  // namespace

std::string CaptureNow(const char* tag) {
  if (!g_presenter) return {};
  rex::ui::RawImage img;
  if (!g_presenter->CaptureGuestOutput(img) || !img.width || !img.height) {
    REXLOG_WARN("Screenshot failed: no guest frame available");
    return {};
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - g_start).count();
  char name[64];
  snprintf(name, sizeof(name), "%s_%07lld.bmp", tag, (long long)ms);
  auto path = ShotDir() / name;
  if (!WriteBmp(path, img)) return {};
  REXLOG_INFO("Screenshot saved: {} ({}x{})", path.string(), img.width, img.height);
  return path.string();
}

std::thread g_burst_thread;

void StartCapture(rex::ui::Presenter* presenter) {
  g_presenter = presenter;
  if (!presenter || g_thread.joinable()) return;
  if (const std::string b = REXCVAR_GET(l4d2_burst); !b.empty()) {
    double start = 0;
    int count = 0, ms = 100;
    if (sscanf(b.c_str(), "%lf:%d:%d", &start, &count, &ms) >= 2 && count > 0) {
      g_burst_thread = std::thread([start, count, ms] {
        const auto t0 = std::chrono::steady_clock::now();
        auto wait_until = [&](std::chrono::steady_clock::time_point t) {
          std::unique_lock lock(g_mutex);
          return !g_cv.wait_until(lock, t, [] { return g_stop; });
        };
        if (!wait_until(t0 + std::chrono::milliseconds(int64_t(start * 1000)))) return;
        auto next = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) {
          CaptureNow("burst");
          next += std::chrono::milliseconds(std::max(10, ms));
          if (!wait_until(next)) return;
        }
      });
    }
  }
  g_thread = std::thread([] {
    std::unique_lock lock(g_mutex);
    while (!g_stop) {
      const int interval = REXCVAR_GET(l4d2_autoshot);
      g_cv.wait_for(lock, std::chrono::seconds(interval > 0 ? interval : 1), [] { return g_stop; });
      if (g_stop) break;
      if (interval > 0 && g_auto_count < REXCVAR_GET(l4d2_autoshot_limit)) {
        lock.unlock();
        if (!CaptureNow("auto").empty()) ++g_auto_count;
        lock.lock();
      }
    }
  });
}

void StopCapture() {
  {
    std::lock_guard lock(g_mutex);
    g_stop = true;
  }
  g_cv.notify_all();
  if (g_thread.joinable()) g_thread.join();
  if (g_burst_thread.joinable()) g_burst_thread.join();
  g_presenter = nullptr;
}

}  // namespace l4d2
