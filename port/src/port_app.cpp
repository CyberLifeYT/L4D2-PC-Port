// L4D2 PC port - application hooks: paths, settings file, crash log.
#include <windows.h>

#include <csignal>
#include <cstdio>
#include <exception>
#include <fstream>
#include <string>


#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>

#include "generated/default/l4d2_init.h"
#include "l4d2_app.h"
#include "capture.h"
#include "overlay.h"

namespace l4d2 {
void StartRawMouse();
void StopRawMouse();
}  // namespace l4d2

REXCVAR_DEFINE_STRING(l4d2_settings_file, "", "L4D2 Debug",
                      "Settings file to apply instead of <exe_dir>\\l4d2_settings.ini");

namespace {

std::filesystem::path ExeDir() { return rex::filesystem::GetExecutableFolder(); }

std::filesystem::path LogDir() {
  auto dir = ExeDir() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

// ---------------------------------------------------------------------------
// Crash log: <exe_dir>\logs\crash.txt (native stack, module-relative).
// ---------------------------------------------------------------------------
std::string g_crash_path;

void WriteStack(FILE* f, const char* header) {
  fprintf(f, "%s\n", header);
  void* frames[48];
  USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
  for (USHORT i = 0; i < n; ++i) {
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)frames[i], &mod)) {
      GetModuleFileNameA(mod, name, MAX_PATH);
    }
    const char* base = strrchr(name, '\\');
    base = base ? base + 1 : name;
    fprintf(f, "  #%02u %p %s+0x%llx\n", i, frames[i], base,
            (unsigned long long)((char*)frames[i] - (char*)mod));
  }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* xp) {
  if (FILE* f = fopen(g_crash_path.c_str(), "a")) {
    fprintf(f, "Unhandled exception 0x%08lX at %p\n",
            (unsigned long)xp->ExceptionRecord->ExceptionCode,
            xp->ExceptionRecord->ExceptionAddress);
    WriteStack(f, "Stack:");
    fclose(f);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashLog() {
  g_crash_path = (LogDir() / "crash.txt").string();
  // Unhandled-only filter: never intercepts the faults the runtime/GPU
  // emulation handle themselves (MMIO, write-watch).
  SetUnhandledExceptionFilter(CrashFilter);
  signal(SIGABRT, [](int) {
    if (FILE* f = fopen(g_crash_path.c_str(), "a")) {
      WriteStack(f, "abort() called:");
      fclose(f);
    }
    _exit(99);
  });
}

// ---------------------------------------------------------------------------
// Settings file: <exe_dir>\l4d2_settings.ini, written by the launcher.
// "name=value" per line, '#' comments, [sections] ignored. Applied after the
// GPU plugin is loaded so its flags exist. Report: logs\settings_applied.txt
// ---------------------------------------------------------------------------
std::string Trim(std::string v) {
  size_t a = v.find_first_not_of(" \t\r\n");
  size_t b = v.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

void ApplySettingsFile() {
  // Load the GPU plugin now so its static flag registrations run; the
  // runtime's own load later returns the same module.
  LoadLibraryW((ExeDir() / L"rexgpu-xenos.dll").c_str());

  std::ofstream out(LogDir() / "settings_applied.txt", std::ios::trunc);
  std::filesystem::path file = ExeDir() / "l4d2_settings.ini";
  if (std::string custom = REXCVAR_GET(l4d2_settings_file); !custom.empty()) file = custom;
  std::ifstream in(file);
  if (!in) {
    out << file.string() << " not found; using defaults\n";
    return;
  }
  out << "# " << file.string() << "\n";
  // Options given on the command line win over the file.
  const std::string cmdline = GetCommandLineA();
  std::string line;
  while (std::getline(in, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    line = Trim(line);
    if (line.empty() || line[0] == '[' || line[0] == ';') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string name = Trim(line.substr(0, eq));
    std::string value = Trim(line.substr(eq + 1));
    if (cmdline.find("--" + name + "=") != std::string::npos ||
        cmdline.find("--" + name + " ") != std::string::npos) {
      out << name << " = (command line overrides file)\n";
      continue;
    }
    bool known = rex::cvar::GetFlagInfo(name) != nullptr;
    bool ok = known && rex::cvar::SetFlagByName(name, value);
    out << name << " = " << value << "  ->  "
        << (!known ? "unknown setting" : ok ? "ok" : "rejected") << "\n";
  }
}

}  // namespace

void L4d2App::OnConfigurePaths(rex::PathConfig& paths) {
  if (!paths.game_data_root.empty()) return;  // --game_data_root given
  auto installed = ExeDir() / "game";
  if (std::filesystem::is_directory(installed)) {
    paths.game_data_root = installed;
    return;
  }
  // Developer tree: rexproject\out\build\<preset>\l4d2.exe -> ..\..\..\..\assets
  auto dev = ExeDir().parent_path().parent_path().parent_path().parent_path() / "assets";
  if (std::filesystem::is_directory(dev)) paths.game_data_root = dev;
}

void L4d2App::OnPostInitLogging() {
  // The audio (XMA) path logs every register write at debug level - thousands
  // of lines per second, a measurable frame-time cost. Keep it at warning.
  l4d2::ApplyLogLevels();
}

void L4d2App::OnPreSetup(rex::RuntimeConfig& config) {
  InstallCrashLog();
  if (!config.graphics && config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
  ApplySettingsFile();
  // Raw mouse rides on the SDK's keyboard/mouse mode for keys, mouse buttons
  // and cursor capture; the inputsystem hook then overwrites the right stick
  // that mode produces with the raw-input value on every poll.
  if (rex::cvar::GetFlagByName("l4d2_raw_mouse") == "true") {
    rex::cvar::SetFlagByName("mnk_mode", "true");
    rex::cvar::SetFlagByName("mnk_mouse", "true");
  }
  // A log_level from the settings file resets every category; re-apply ours.
  l4d2::ApplyLogLevels();
}

void L4d2App::OnPostSetup() {
  l4d2::ApplyLogLevels();
  if (auto* rt = runtime(); rt && rt->graphics_system()) {
    l4d2::StartCapture(rt->graphics_system()->presenter());
    l4d2::SetGraphicsSystem(rt->graphics_system());
  }
  l4d2::StartRawMouse();
}

void L4d2App::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  perf_overlay_ = l4d2::CreatePerfOverlay(drawer);
  rex::ui::RegisterBind("bind_l4d2_overlay", "F2", "Toggle L4D2 performance overlay",
                        [] { l4d2::TogglePerfOverlay(); });
  rex::ui::RegisterBind("bind_l4d2_screenshot", "F12", "Save a screenshot to logs\\shots",
                        [] { l4d2::CaptureNow("shot"); });
}

void L4d2App::OnShutdown() {
  rex::ui::UnregisterBind("bind_l4d2_overlay");
  rex::ui::UnregisterBind("bind_l4d2_screenshot");
  l4d2::StopCapture();
  l4d2::StopRawMouse();
  perf_overlay_.reset();
}
