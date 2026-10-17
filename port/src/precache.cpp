// L4D2 PC port - shader cache builder (game side).
//
// With l4d2_precache_maps set (the launcher's "Build shader cache" window
// does this), the game loads every listed map in turn, sweeps the camera all
// the way round and up and down so the world, the survivors and the infected
// are drawn, and waits until no new shader pipelines are being compiled. The
// pipelines go into the normal shader storage (store_shaders), so the first
// real play-through no longer stutters while they compile.
//
// Progress is written to logs\precache.json once a second for the launcher:
//   {"state":"running","index":3,"total":23,"map":"c1m4_atrium",
//    "phase":"loading","pipelines":1234,"elapsed":412}
// state is "starting", "running", "done" or "failed".
//
// Map loads are detected from the client's world view: CViewRender::SetUpView
// (src/stubs/hook_client.cpp counts its calls) stops while a map is loading
// and runs every frame once the map is up.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>

REXCVAR_DEFINE_STRING(l4d2_precache_maps, "", "L4D2 Debug",
                      "Shader cache builder: comma-separated maps to load and look around in, then quit");
REXCVAR_DEFINE_INT32(l4d2_precache_look_seconds, 24, "L4D2 Debug",
                     "Shader cache builder: seconds of camera sweep per map");

namespace l4d2 {
uint64_t PipelinesCreated();                      // overlay.cpp
void QueueGameCommand(const std::string& cmd);    // console.cpp
void SetForcedLook(int x, int y);                 // pcinput.cpp
}  // namespace l4d2

namespace {

std::atomic<uint64_t> g_view_calls{0};
std::atomic<bool> g_running{false};
std::thread g_thread;

using Clock = std::chrono::steady_clock;

double Since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

void WriteProgress(const char* state, int index, int total, const std::string& map, const char* phase,
                   double elapsed) {
  const auto dir = rex::filesystem::GetExecutableFolder() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto tmp = dir / "precache.json.tmp";
  FILE* f = _wfopen(tmp.c_str(), L"w");
  if (!f) return;
  fprintf(f,
          "{\"state\":\"%s\",\"index\":%d,\"total\":%d,\"map\":\"%s\",\"phase\":\"%s\",\"pipelines\":%llu,"
          "\"elapsed\":%.0f}\n",
          state, index, total, map.c_str(), phase, (unsigned long long)l4d2::PipelinesCreated(), elapsed);
  fclose(f);
  std::filesystem::rename(tmp, dir / "precache.json", ec);
}

// World-view frames per second over the last `window` seconds.
double ViewRate(double window) {
  const uint64_t a = g_view_calls.load();
  Sleep(DWORD(window * 1000));
  return double(g_view_calls.load() - a) / window;
}

void Worker(std::vector<std::string> maps) {
  const auto start = Clock::now();
  const int total = int(maps.size());
  std::string cur;
  int index = 0;
  const char* phase = "starting";
  std::mutex m;
  std::atomic<bool> writer_on{true};
  std::thread writer([&] {
    while (writer_on && g_running) {
      {
        std::lock_guard<std::mutex> lock(m);
        WriteProgress(index == 0 ? "starting" : "running", index, total, cur, phase, Since(start));
      }
      Sleep(1000);
    }
  });
  auto set = [&](int i, const std::string& map, const char* ph) {
    std::lock_guard<std::mutex> lock(m);
    index = i;
    cur = map;
    phase = ph;
  };
  // Main menu first (-novid and the automatic START press get there in ~20 s).
  for (int i = 0; i < 25 && g_running; ++i) Sleep(1000);
  l4d2::QueueGameCommand("sv_cheats 1");
  for (int i = 0; i < total && g_running; ++i) {
    const std::string& map = maps[i];
    set(i + 1, map, "loading");
    l4d2::QueueGameCommand("map " + map);
    // Loading: the world view stops (or never ran) ...
    const auto issued = Clock::now();
    while (g_running && Since(issued) < 30 && ViewRate(1.0) > 5) {}
    // ... and comes back once the map is up.
    int good = 0;
    while (g_running && Since(issued) < 240 && good < 3) good = ViewRate(1.0) >= 15 ? good + 1 : 0;
    if (!g_running) break;
    l4d2::QueueGameCommand("god 1");
    Sleep(1500);
    // Look all the way round, then up and down while turning.
    set(i + 1, map, "looking");
    const double look = std::max(8, REXCVAR_GET(l4d2_precache_look_seconds));
    const auto t0 = Clock::now();
    while (g_running && Since(t0) < look) {
      const double f = Since(t0) / look;
      const int ry = f < 0.45 ? 0 : f < 0.65 ? 26000 : f < 0.9 ? -26000 : 0;
      l4d2::SetForcedLook(32000, ry);
      Sleep(50);
    }
    l4d2::SetForcedLook(INT32_MIN, 0);
    // Wait for the compile queue to drain: 6 s without a new pipeline.
    set(i + 1, map, "compiling");
    uint64_t last = l4d2::PipelinesCreated();
    auto quiet = Clock::now();
    const auto t1 = Clock::now();
    while (g_running && Since(t1) < 45 && Since(quiet) < 6) {
      Sleep(500);
      const uint64_t p = l4d2::PipelinesCreated();
      if (p != last) {
        last = p;
        quiet = Clock::now();
      }
    }
  }
  writer_on = false;
  writer.join();
  WriteProgress(g_running ? "done" : "failed", total, total, "", "done", Since(start));
  if (g_running) {
    Sleep(1500);
    // Leave through the engine so the shader storage is flushed normally.
    l4d2::QueueGameCommand("quit");
  }
}

}  // namespace

namespace l4d2 {
void StartPrecache() {
  const std::string list = REXCVAR_GET(l4d2_precache_maps);
  if (list.empty() || g_running.exchange(true)) return;
  std::vector<std::string> maps;
  size_t pos = 0;
  while (pos <= list.size()) {
    size_t c = list.find(',', pos);
    if (c == std::string::npos) c = list.size();
    std::string m = list.substr(pos, c - pos);
    while (!m.empty() && (m.front() == ' ')) m.erase(m.begin());
    while (!m.empty() && (m.back() == ' ')) m.pop_back();
    bool ok = !m.empty();
    for (char ch : m) ok = ok && (isalnum((unsigned char)ch) || ch == '_');
    if (ok) maps.push_back(m);
    pos = c + 1;
  }
  if (maps.empty()) return;
  g_thread = std::thread(Worker, maps);
}
void StopPrecache() {
  if (!g_running.exchange(false)) return;
  l4d2::SetForcedLook(INT32_MIN, 0);
  if (g_thread.joinable()) g_thread.join();
}
}  // namespace l4d2

// Called by the client hook on every CViewRender::SetUpView.
extern "C" __declspec(dllexport) void L4d2NoteWorldView() { g_view_calls.fetch_add(1, std::memory_order_relaxed); }
