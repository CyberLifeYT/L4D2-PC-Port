// L4D2 PC port - in-game performance overlay.
//
// Everything shown is measured live:
//  - Present FPS / frame time / 1% low: host frame pacing (ImGui delta time)
//  - Game FPS / draw calls / cache hit rates: runtime perf counters
//  - Shader pipelines: counted from the GPU backend as they are compiled
//    (a burst of compiles = shader-compilation stutter)
//  - Memory: process working set / commit
//  - Resolution / scale / render path: the live GPU settings
//
// All toggles are cvars (category "L4D2 Overlay"): set them in
// l4d2_settings.ini (the launcher does this), live in the F4 settings
// overlay, or toggle the whole overlay with F2.
#include "overlay.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <spdlog/sinks/base_sink.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/ui/imgui_dialog.h>

REXCVAR_DEFINE_BOOL(l4d2_overlay, false, "L4D2 Overlay", "Show the L4D2 performance overlay (F2)");
REXCVAR_DEFINE_BOOL(l4d2_overlay_fps, true, "L4D2 Overlay", "Overlay: FPS and frame time");
REXCVAR_DEFINE_BOOL(l4d2_overlay_graph, true, "L4D2 Overlay", "Overlay: frame-time graph");
REXCVAR_DEFINE_BOOL(l4d2_overlay_render, true, "L4D2 Overlay", "Overlay: render/shader statistics");
REXCVAR_DEFINE_BOOL(l4d2_overlay_memory, true, "L4D2 Overlay", "Overlay: memory usage");
REXCVAR_DEFINE_BOOL(l4d2_overlay_settings, true, "L4D2 Overlay", "Overlay: active GPU settings");
REXCVAR_DEFINE_BOOL(l4d2_shader_toast, true, "L4D2 Overlay",
                    "Show a small notice at the bottom of the screen while shaders compile");
REXCVAR_DEFINE_BOOL(l4d2_perf_log, true, "L4D2 Overlay",
                    "Append FPS/frame-time/shader stats every 5 s to logs\\perf_summary.csv");
REXCVAR_DEFINE_INT32(l4d2_overlay_corner, 0, "L4D2 Overlay",
                     "Overlay corner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right");

namespace l4d2 {
namespace {

// ---------------------------------------------------------------- GPU sink
std::atomic<uint64_t> g_pipelines{0};
std::atomic<uint64_t> g_render_targets{0};
std::atomic<uint64_t> g_cached_shaders{0};
std::atomic<uint64_t> g_cached_pipelines{0};
std::mutex g_adapter_mutex;
std::string g_adapter;

class GpuStatsSink : public spdlog::sinks::base_sink<std::mutex> {
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    std::string_view text(msg.payload.data(), msg.payload.size());
    if (text.starts_with("Creating graphics pipeline")) {
      g_pipelines.fetch_add(1, std::memory_order_relaxed);
    } else if (text.starts_with("Created a ") && text.find("render target") != text.npos) {
      g_render_targets.fetch_add(1, std::memory_order_relaxed);
    } else if (text.starts_with("Translated ") && text.find("from the storage") != text.npos) {
      g_cached_shaders = std::strtoull(std::string(text.substr(11, 12)).c_str(), nullptr, 10);
    } else if (text.starts_with("Created ") && text.find("pipelines") != text.npos &&
               text.find("from the storage") != text.npos) {
      g_cached_pipelines = std::strtoull(std::string(text.substr(8, 12)).c_str(), nullptr, 10);
    } else if (text.starts_with("DXGI adapter: ")) {
      std::lock_guard lock(g_adapter_mutex);
      g_adapter = std::string(text.substr(14));
      if (auto p = g_adapter.find(" (vendor"); p != std::string::npos) g_adapter.resize(p);
    }
  }
  void flush_() override {}
};

// ------------------------------------------------------------ the overlay
class PerfOverlay;
PerfOverlay* g_overlay = nullptr;

class PerfOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit PerfOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) { g_overlay = this; }
  ~PerfOverlay() override {
    if (g_overlay == this) g_overlay = nullptr;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    // The runtime re-initialises logging after setup (and REX_LOG_LEVEL can
    // force debug); re-assert the per-category levels periodically so the
    // audio register spam never comes back. SetCategoryLevel is cheap.
    if ((frame_counter_++ % 600) == 0) ApplyLogLevels();
    Sample(io);
    DrawShaderToast(io);
    if (!REXCVAR_GET(l4d2_overlay)) return;

    const int corner = std::clamp(REXCVAR_GET(l4d2_overlay_corner), 0, 3);
    const float pad = 12.0f;
    ImVec2 pos(corner & 1 ? io.DisplaySize.x - pad : pad, corner & 2 ? io.DisplaySize.y - pad : pad);
    ImVec2 pivot(corner & 1 ? 1.0f : 0.0f, corner & 2 ? 1.0f : 0.0f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.05f, 0.05f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.62f, 0.08f, 0.06f, 0.9f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##l4d2_perf", nullptr, flags)) {
      const ImVec4 red(0.86f, 0.16f, 0.12f, 1.0f), dim(0.62f, 0.60f, 0.56f, 1.0f),
          good(0.55f, 0.85f, 0.35f, 1.0f), warn(0.95f, 0.75f, 0.20f, 1.0f);
      ImGui::TextColored(red, "L4D2 PORT");
      ImGui::SameLine();
      ImGui::TextColored(dim, "F2 hide  F4 settings");

      if (REXCVAR_GET(l4d2_overlay_fps)) {
        ImGui::Separator();
        const ImVec4 fps_col = fps_ >= 55 ? good : fps_ >= 30 ? warn : red;
        ImGui::TextColored(fps_col, "%5.1f FPS (display)", fps_);
        ImGui::SameLine();
        ImGui::Text("%5.2f ms", frame_ms_);
        ImGui::SameLine();
        ImGui::TextColored(dim, "1%% low %.0f", low1_fps_);
        if (game_fps_ > 0) {
          ImGui::TextColored(game_fps_ >= 55 ? good : game_fps_ >= 28 ? warn : red, "Game  %5.1f FPS   %5.2f ms",
                             game_fps_, game_ms_);
        }
        ImGui::TextColored(dim, "Stutters (>2x avg): %llu   worst %.1f ms",
                           (unsigned long long)stutters_, worst_ms_);
      }

      if (REXCVAR_GET(l4d2_overlay_graph)) {
        float max_ms = 33.4f;
        for (float v : history_) max_ms = std::max(max_ms, v);
        char label[32];
        snprintf(label, sizeof(label), "%.0f ms", max_ms);
        ImGui::PushStyleColor(ImGuiCol_PlotLines, red);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.10f, 0.10f, 1.0f));
        ImGui::PlotLines("##ft", history_.data(), (int)history_.size(), (int)history_pos_, label, 0.0f,
                         max_ms, ImVec2(280, 52));
        ImGui::PopStyleColor(2);
      }

      if (REXCVAR_GET(l4d2_overlay_render)) {
        ImGui::Separator();
        ImGui::Text("Output   %.0fx%.0f   guest %sx%s   scale %sx", io.DisplaySize.x, io.DisplaySize.y,
                    Flag("video_mode_width").c_str(), Flag("video_mode_height").c_str(),
                    Flag("resolution_scale").c_str());
        if (draw_calls_ > 0) ImGui::Text("Draw calls / frame   %lld", (long long)draw_calls_);
        ImGui::Text("Shader pipelines   %llu  (+%llu last s)", (unsigned long long)g_pipelines.load(),
                    (unsigned long long)pipelines_last_sec_);
        if (pipelines_last_sec_ > 0) {
          ImGui::SameLine();
          ImGui::TextColored(warn, "compiling");
        }
        ImGui::Text("Render targets created   %llu", (unsigned long long)g_render_targets.load());
        if (tex_hit_pct_ >= 0) ImGui::Text("Texture cache hit   %.1f%%", tex_hit_pct_);
        if (pipe_hit_pct_ >= 0) ImGui::Text("Pipeline cache hit  %.1f%%", pipe_hit_pct_);
        std::lock_guard lock(g_adapter_mutex);
        if (!g_adapter.empty()) ImGui::TextColored(dim, "GPU  %s  (Direct3D 12)", g_adapter.c_str());
      }

      if (REXCVAR_GET(l4d2_overlay_memory)) {
        ImGui::Separator();
        ImGui::Text("Memory   working set %.0f MB   commit %.0f MB", ws_mb_, commit_mb_);
      }

      if (REXCVAR_GET(l4d2_overlay_settings)) {
        ImGui::Separator();
        std::string path = Flag("render_target_path_d3d12");
        ImGui::TextColored(dim, "RT path %s  readback %s  vsync %s  aniso %s",
                           path.empty() ? "rtv" : path.c_str(), Flag("readback_resolve").c_str(),
                           Flag("vsync").c_str(), Flag("anisotropic_override").c_str());
      }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
  }

 private:
  static std::string Flag(const char* name) { return rex::cvar::GetFlagByName(name); }

  // Bottom-centre notice while new shader pipelines are being compiled. New
  // pipelines are built in the background; until one is ready the draws that
  // need it are skipped, which is the pop-in/stutter players notice.
  void DrawShaderToast(ImGuiIO& io) {
    if (!REXCVAR_GET(l4d2_shader_toast)) return;
    const auto now = std::chrono::steady_clock::now();
    const uint64_t total = g_pipelines.load(std::memory_order_relaxed);
    if (total != toast_seen_) {
      toast_session_ += total - toast_seen_;
      toast_seen_ = total;
      toast_last_ = now;
      if (toast_burst_start_ == std::chrono::steady_clock::time_point{}) {
        toast_burst_start_ = now;
        toast_burst_base_ = toast_session_ - 1;
      }
    }
    const float since = std::chrono::duration<float>(now - toast_last_).count();
    const float uptime = std::chrono::duration<float>(now - start_).count();
    const bool startup = uptime < 8.0f && g_cached_pipelines.load() > 0;
    if (since > 2.5f && !startup) {
      toast_burst_start_ = {};
      return;
    }
    char line[160];
    if (since <= 2.5f) {
      const uint64_t burst = toast_session_ - toast_burst_base_;
      snprintf(line, sizeof(line), "Compiling shaders  %c  %llu new  (%llu this session)",
               "|/-\\"[(frame_counter_ / 6) % 4], (unsigned long long)burst,
               (unsigned long long)toast_session_);
    } else {
      snprintf(line, sizeof(line), "Shader cache loaded: %llu shaders, %llu pipelines",
               (unsigned long long)g_cached_shaders.load(), (unsigned long long)g_cached_pipelines.load());
    }
    const float alpha = since <= 2.5f ? 1.0f : std::clamp(8.0f - uptime, 0.0f, 1.0f);
    const ImVec2 size = ImGui::CalcTextSize(line);
    const ImVec2 pos((io.DisplaySize.x - size.x) * 0.5f - 14.0f, io.DisplaySize.y - size.y - 40.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x + 28.0f, pos.y + size.y + 14.0f),
                      IM_COL32(12, 10, 9, int(200 * alpha)), 2.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + 4.0f, pos.y + size.y + 14.0f),
                      IM_COL32(200, 30, 22, int(255 * alpha)));
    dl->AddText(ImVec2(pos.x + 16.0f, pos.y + 7.0f), IM_COL32(233, 226, 211, int(255 * alpha)), line);
  }

  void WritePerfLog(float secs) {
    if (!REXCVAR_GET(l4d2_perf_log)) return;
    static FILE* f = nullptr;
    if (!f) {
      wchar_t exe[MAX_PATH];
      GetModuleFileNameW(nullptr, exe, MAX_PATH);
      std::wstring dir(exe);
      dir.resize(dir.find_last_of(L'\\') + 1);
      dir += L"logs";
      CreateDirectoryW(dir.c_str(), nullptr);
      f = _wfopen((dir + L"\\perf_summary.csv").c_str(), L"w");
      if (!f) return;
      fprintf(f, "time_s,ui_fps,ui_avg_ms,ui_low1_fps,ui_worst_ms,stutters,game_fps,game_min_fps,draw_calls,pipelines_total,"
                 "pipelines_5s,working_set_mb,rt_path,readback\n");
    }
    const uint64_t p = g_pipelines.load();
    std::string path = Flag("render_target_path_d3d12");
    fprintf(f, "%.0f,%.1f,%.2f,%.1f,%.1f,%llu,%.1f,%.1f,%lld,%llu,%llu,%.0f,%s,%s\n", secs, fps_, frame_ms_,
            low1_fps_, worst_ms_, (unsigned long long)stutters_, game_fps_, game_low_fps_, (long long)draw_calls_,
            (unsigned long long)p, (unsigned long long)(p - perf_log_prev_), ws_mb_,
            path.empty() ? "rtv" : path.c_str(), Flag("readback_resolve").c_str());
    fflush(f);
    perf_log_prev_ = p;
    worst_ms_ = 0;  // worst frame per interval
    game_low_fps_ = 0;
  }

  uint64_t frame_counter_ = 0;
  uint64_t toast_seen_ = 0, toast_session_ = 0, toast_burst_base_ = 0, perf_log_prev_ = 0;
  std::chrono::steady_clock::time_point toast_last_{}, toast_burst_start_{};
  const std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
  int perf_tick_ = 0;
  uint64_t game_prev_frames_ = 0;
  float game_low_fps_ = 0;  // lowest 1-s game FPS in the current perf-log interval
  std::chrono::steady_clock::time_point game_prev_time_{};
  void Sample(ImGuiIO& io) {
    const float ms = io.DeltaTime * 1000.0f;
    if (ms <= 0.0f || ms > 2000.0f) return;
    history_[history_pos_] = ms;
    history_pos_ = (history_pos_ + 1) % history_.size();
    if (filled_ < history_.size()) ++filled_;

    // Rolling stats over the history window.
    float sum = 0.0f;
    std::vector<float> sorted(history_.begin(), history_.begin() + filled_);
    for (float v : sorted) sum += v;
    const float avg = sum / filled_;
    frame_ms_ = avg;
    fps_ = avg > 0 ? 1000.0f / avg : 0.0f;
    std::sort(sorted.begin(), sorted.end(), std::greater<float>());
    const size_t idx = std::max<size_t>(1, filled_ / 100) - 1;
    low1_fps_ = sorted[idx] > 0 ? 1000.0f / sorted[idx] : 0.0f;
    if (filled_ > 30 && ms > 2.0f * avg) {
      ++stutters_;
      worst_ms_ = std::max(worst_ms_, ms);
    }

    // Once-per-second sampling of heavier stats.
    auto now = std::chrono::steady_clock::now();
    if (now - last_sec_ < std::chrono::seconds(1)) return;
    last_sec_ = now;

    const uint64_t p = g_pipelines.load();
    pipelines_last_sec_ = p - pipelines_prev_;
    pipelines_prev_ = p;

    using rex::perf::CounterId;
    using rex::perf::GetSnapshotCounter;
    draw_calls_ = GetSnapshotCounter(CounterId::kDrawCalls);
    // Game FPS = guest frames submitted per second (the UI/present rate can
    // differ: the presenter refreshes at the display rate).
    const uint64_t gf = GuestFrameCount();
    const float dt = std::chrono::duration<float>(now - game_prev_time_).count();
    if (gf && game_prev_frames_ && dt > 0) {
      game_fps_ = (gf - game_prev_frames_) / dt;
      game_ms_ = game_fps_ > 0 ? 1000.0f / game_fps_ : 0.0f;
      game_low_fps_ = game_low_fps_ == 0 ? game_fps_ : std::min(game_low_fps_, game_fps_);
    }
    game_prev_frames_ = gf;
    game_prev_time_ = now;
    tex_hit_pct_ = Pct(GetSnapshotCounter(CounterId::kTextureCacheHits),
                       GetSnapshotCounter(CounterId::kTextureCacheMisses));
    pipe_hit_pct_ = Pct(GetSnapshotCounter(CounterId::kPipelineCacheHits),
                        GetSnapshotCounter(CounterId::kPipelineCacheMisses));

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
      ws_mb_ = pmc.WorkingSetSize / (1024.0f * 1024.0f);
      commit_mb_ = pmc.PrivateUsage / (1024.0f * 1024.0f);
    }
    if (++perf_tick_ % 5 == 0) {
      WritePerfLog(std::chrono::duration<float>(now - start_).count());
    }
  }

  static float Pct(int64_t hits, int64_t misses) {
    const int64_t total = hits + misses;
    return total > 0 ? 100.0f * hits / total : -1.0f;
  }

  std::array<float, 240> history_{};
  size_t history_pos_ = 0, filled_ = 0;
  float fps_ = 0, frame_ms_ = 0, low1_fps_ = 0, worst_ms_ = 0;
  float game_fps_ = 0, game_ms_ = 0, tex_hit_pct_ = -1, pipe_hit_pct_ = -1;
  float ws_mb_ = 0, commit_mb_ = 0;
  int64_t draw_calls_ = 0;
  uint64_t stutters_ = 0, pipelines_prev_ = 0, pipelines_last_sec_ = 0;
  std::chrono::steady_clock::time_point last_sec_{};
};

}  // namespace

void ApplyLogLevels() {
  // The audio (XMA) path logs every register write at debug level - thousands
  // of lines per second, a measurable frame-time cost. Keep it at warning.
  if (auto apu = rex::FindCategory("apu")) rex::SetCategoryLevel(*apu, spdlog::level::warn);
  auto gpu = rex::FindCategory("gpu");
  if (!gpu) return;
  static bool attached = false;
  if (!attached) {
    attached = true;
    rex::AddSink(*gpu, std::make_shared<GpuStatsSink>());
  }
  // Pipeline/render-target creation is reported at debug level; these are
  // low-volume (once per new shader), unlike the audio spam.
  rex::SetCategoryLevel(*gpu, spdlog::level::debug);
}

std::unique_ptr<rex::ui::ImGuiDialog> CreatePerfOverlay(rex::ui::ImGuiDrawer* drawer) {
  return std::make_unique<PerfOverlay>(drawer);
}

void TogglePerfOverlay() { REXCVAR_SET(l4d2_overlay, !REXCVAR_GET(l4d2_overlay)); }

}  // namespace l4d2
