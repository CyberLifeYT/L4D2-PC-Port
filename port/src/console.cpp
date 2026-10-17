// L4D2 PC port - game console bridge for mods and settings.
//
// Runs Source console commands inside the game ("fov_desired 100", ...). The
// inputsystem hook asks L4d2NextConsoleCommand() for work on every pad poll
// and hands each command to the engine's own Cbuf_AddText, so commands go
// through the game's normal command buffer with all its side effects
// (change callbacks, user-info updates to the server, etc).
//
// Commands come from l4d2_console_commands (';'-separated, written by the
// launcher from the enabled mods' mod.json files) and are re-applied every
// l4d2_console_interval seconds, because the game reloads its profile and
// config on map changes and would otherwise put values back.
//
// The 360 build hides many ConVars from the console (development-only flag).
// Before a command runs, the ConVar it names is looked up in guest memory and
// its FCVAR_DEVELOPMENTONLY / FCVAR_HIDDEN flags are cleared, so mods can use
// the same commands as on PC. Results: logs\console.txt
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <intrin.h>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>

REXCVAR_DEFINE_STRING(l4d2_console_commands, "", "L4D2 Mods",
                      "Console commands run in the game, ';'-separated (written by the launcher from enabled mods)");
REXCVAR_DEFINE_INT32(l4d2_console_interval, 10, "L4D2 Mods",
                     "Seconds between re-applying l4d2_console_commands (0 = once)");
REXCVAR_DEFINE_STRING(l4d2_mod_launch_options, "", "L4D2 Mods",
                      "Game command-line switches from enabled mods (e.g. -novid), added to cl");
REXCVAR_DEFINE_STRING(l4d2_test_commands, "", "L4D2 Debug",
                      "Automated tests: 'seconds:command|seconds:command' run once at those times");

namespace {

std::mutex g_mutex;
uint8_t* g_base = nullptr;
std::atomic<bool> g_running{false};
std::thread g_thread;
std::map<std::string, int> g_unlock;  // name -> 1 unlocked, 0 not found yet, -1 gave up
std::map<std::string, uint32_t> g_convar_addr;  // name -> guest address of the ConVar
std::set<std::string> g_logged_unknown;

void Log(const char* fmt, ...) {
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  static FILE* f = [] {
    auto dir = rex::filesystem::GetExecutableFolder() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return _wfopen((dir / "console.txt").c_str(), L"w");
  }();
  if (!f) return;
  static const auto t0 = std::chrono::steady_clock::now();
  fprintf(f, "[%7.1f] ", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fflush(f);
}

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= s.size()) {
    size_t c = s.find(sep, pos);
    if (c == std::string::npos) c = s.size();
    std::string part = s.substr(pos, c - pos);
    size_t a = part.find_first_not_of(" \t\r\n"), b = part.find_last_not_of(" \t\r\n");
    if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
    pos = c + 1;
  }
  return out;
}

std::string FirstWord(const std::string& cmd) {
  size_t e = cmd.find_first_of(" \t");
  return cmd.substr(0, e);
}

// Committed, readable guest pages inside the module images.
struct Region { uint32_t lo, hi; };
std::vector<Region> ModuleRegions() {
  std::vector<Region> out;
  for (uint32_t a = 0x82000000; a < 0x8A000000;) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(g_base + a, &mbi, sizeof(mbi))) { a += 0x10000; continue; }
    const uint64_t end = uint64_t(static_cast<uint8_t*>(mbi.BaseAddress) - g_base) + mbi.RegionSize;
    const uint32_t hi = uint32_t(std::min<uint64_t>(end, 0x8A000000));
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && hi > a) {
      if (!out.empty() && out.back().hi == a) out.back().hi = hi; else out.push_back({a, hi});
    }
    a = std::max(hi, a + 0x1000);
  }
  return out;
}

bool InModules(uint32_t a, const std::vector<Region>& rs) {
  for (auto& r : rs) if (a >= r.lo && a < r.hi) return true;
  return false;
}

// Finds the ConVar named `name` and clears FCVAR_DEVELOPMENTONLY (0x2) and
// FCVAR_HIDDEN (0x10). Layout in this build (checked on fps_max): name ptr
// +0x0C, flags +0x14, parent +0x1C, default string +0x20.
int Unlock(const std::string& name) {
  const auto rs = ModuleRegions();
  std::string pat;
  pat.push_back('\0');
  pat += name;
  pat.push_back('\0');
  int found = 0;
  const std::vector<uint8_t> pv(pat.begin(), pat.end());
  const std::boyer_moore_horspool_searcher searcher(pv.begin(), pv.end());
  for (const auto& r : rs) {
    const uint8_t* lo = g_base + r.lo;
    const uint8_t* hi = g_base + r.hi;
    for (const uint8_t* s = lo;;) {
      s = std::search(s, hi, searcher);
      if (s == hi) break;
      const uint32_t str = uint32_t(s + 1 - g_base);
      const uint32_t want = _byteswap_ulong(str);  // big-endian pointer as stored
      // Pointers to the name: a ConVar has it at +0x0C.
      for (const auto& r2 : rs) {
        for (uint32_t a = (r2.lo + 3) & ~3u; a + 0x30 <= r2.hi; a += 4) {
          uint32_t v;
          memcpy(&v, g_base + a, 4);
          if (v != want || a < 0xC) continue;
          const uint32_t cv = a - 0xC;
          const uint8_t* c = g_base + cv;
          const uint32_t parent = Be32(c + 0x1C), def = Be32(c + 0x20), flags = Be32(c + 0x14);
          if (parent != cv || !InModules(def, rs) || (flags & 0xF0000000)) continue;
          const uint32_t nf = flags & ~0x12u;
          if (nf != flags) {
            uint8_t* w = g_base + cv + 0x14;
            w[0] = uint8_t(nf >> 24); w[1] = uint8_t(nf >> 16); w[2] = uint8_t(nf >> 8); w[3] = uint8_t(nf);
          }
          Log("convar %s at 0x%08X flags 0x%X -> 0x%X", name.c_str(), cv, flags, nf);
          if (!g_convar_addr.count(name)) g_convar_addr[name] = cv;
          ++found;
        }
      }
      s += pat.size() - 1;
    }
  }
  return found;
}

struct Queued { std::string cmd; double boost; };
std::deque<Queued> g_queue2;
void Enqueue(const std::string& cmd, double boost = 0.0) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_queue2.size() < 256) g_queue2.push_back({cmd, boost});
}

bool ReadConVarFloat(const std::string& name, float* out) {
  auto it = g_convar_addr.find(name);
  if (it == g_convar_addr.end()) {
    if (Unlock(name) <= 0) return false;
    it = g_convar_addr.find(name);
    if (it == g_convar_addr.end()) return false;
  }
  const uint32_t parent = Be32(g_base + it->second + 0x1C);
  uint32_t bits = Be32(g_base + parent + 0x2C);
  memcpy(out, &bits, 4);
  return true;
}

std::string Num(double v) {
  char b[32];
  snprintf(b, sizeof(b), "%.4g", v);
  return b;
}

// Look-speed boost while the mouse is in use. The stick path caps the turn
// rate at full deflection; multiplying the game's joy_yaw/pitchsensitivity
// by the boost (and dividing the mouse mapping by it, see pcinput.cpp) keeps
// slow aiming identical and raises the cap for fast flicks. A controller
// gets its normal sensitivity back two seconds after the mouse stops.
std::atomic<double> g_applied_boost{1.0};
std::atomic<int64_t> g_last_mouse_ms{0};
float g_orig_yaw = -1.0f, g_orig_pitch = 1.0f;
bool g_have_orig = false;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void UpdateBoost(std::vector<std::string>& cmds, std::vector<double>& boosts) {
  static bool pc = rex::cvar::GetFlagByName("l4d2_pc_controls") == "true" ||
                   rex::cvar::GetFlagByName("l4d2_raw_mouse") == "true";
  const double want_boost = std::clamp(atof(rex::cvar::GetFlagByName("l4d2_mouse_turn_boost").c_str()), 1.0, 8.0);
  const bool mouse = pc && NowMs() - g_last_mouse_ms.load() < 2000;
  const double target = mouse ? want_boost : 1.0;
  static double requested = 1.0;
  if (target == requested) return;
  if (!g_have_orig && target > 1.0) {
    float y, p;
    if (!ReadConVarFloat("joy_yawsensitivity", &y) || !ReadConVarFloat("joy_pitchsensitivity", &p)) return;
    // A value far above the console's range means a boosted value was saved
    // to the profile last session; fall back to the defaults.
    if (std::fabs(y) > 2.5f || std::fabs(p) > 2.5f || y == 0 || p == 0) { y = -1.0f; p = 1.0f; }
    g_orig_yaw = y;
    g_orig_pitch = p;
    g_have_orig = true;
    Log("look sensitivity: yaw %.3g pitch %.3g", y, p);
  }
  if (!g_have_orig) return;
  requested = target;
  cmds.push_back("joy_yawsensitivity " + Num(g_orig_yaw * target));
  boosts.push_back(-1);  // applied together with the next entry
  cmds.push_back("joy_pitchsensitivity " + Num(g_orig_pitch * target));
  boosts.push_back(target);
}

void Worker() {
  using clock = std::chrono::steady_clock;
  const auto start = clock::now();
  auto next_apply = start + std::chrono::seconds(8);  // engine and client are up
  std::string last;
  struct Timed { double at; std::string cmd; bool done; };
  std::vector<Timed> timed;
  for (const auto& item : Split(REXCVAR_GET(l4d2_test_commands), '|')) {
    const size_t c = item.find(':');
    if (c != std::string::npos) timed.push_back({atof(item.substr(0, c).c_str()), item.substr(c + 1), false});
  }
  while (g_running) {
    Sleep(250);
    if (!g_base) continue;
    const auto now = clock::now();
    const double t = std::chrono::duration<double>(now - start).count();
    std::vector<std::string> cmds;
    const std::string cur = REXCVAR_GET(l4d2_console_commands);
    const int interval = REXCVAR_GET(l4d2_console_interval);
    if (now >= next_apply || cur != last) {
      if (!cur.empty()) cmds = Split(cur, ';');
      next_apply = interval > 0 ? now + std::chrono::seconds(interval) : clock::time_point::max();
      if (cur != last) Log("commands: %s", cur.empty() ? "(none)" : cur.c_str());
      last = cur;
    }
    for (auto& tc : timed) {
      if (!tc.done && t >= tc.at) {
        tc.done = true;
        cmds.push_back(tc.cmd);
        Log("test command at %.0f s: %s", t, tc.cmd.c_str());
      }
    }
    // Port settings ("l4d2_fov 100") are set here instead of in the game.
    std::vector<std::string> game_cmds;
    for (const auto& cmd : cmds) {
      const std::string name = FirstWord(cmd);
      if (name.rfind("l4d2_", 0) == 0 && rex::cvar::GetFlagInfo(name)) {
        std::string value = cmd.size() > name.size() ? cmd.substr(name.size() + 1) : "";
        const std::string current = rex::cvar::GetFlagByName(name);
        // Toggles arrive as 1/0; boolean settings read back as true/false.
        if (current == "true" || current == "false") {
          if (value == "1") value = "true";
          if (value == "0") value = "false";
        }
        if (current != value) {
          rex::cvar::SetFlagByName(name, value);
          Log("port setting %s = %s", name.c_str(), value.c_str());
        }
      } else {
        game_cmds.push_back(cmd);
      }
    }
    // FOV: the server only re-reads it when the player's settings change, so
    // nudge fov_desired after l4d2_fov changes (hook_server.cpp applies it).
    static std::string last_fov = "0";
    const std::string fov = rex::cvar::GetFlagByName("l4d2_fov");
    if (fov != last_fov) {
      last_fov = fov;
      static bool flip = false;
      flip = !flip;
      game_cmds.push_back(flip ? "fov_desired 89" : "fov_desired 90");
      game_cmds.push_back(flip ? "fov_desired 90" : "fov_desired 89");
      Log("fov -> %s", fov.c_str());
    }
    std::vector<double> boosts(game_cmds.size(), 0.0);
    UpdateBoost(game_cmds, boosts);
    for (size_t ci = 0; ci < game_cmds.size(); ++ci) {
      const std::string& cmd = game_cmds[ci];
      const std::string name = FirstWord(cmd);
      int& st = g_unlock[name];
      if (st == 0 && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos) {
        static std::map<std::string, int> tries;
        const int n = Unlock(name);
        if (n > 0) st = 1;
        else if (++tries[name] >= 12) {  // ~2 minutes of retries: not a ConVar
          st = -1;
          Log("%s: no ConVar found (a command, or not in this build)", name.c_str());
        }
      }
      Enqueue(cmd, ci < boosts.size() ? boosts[ci] : 0.0);
    }
  }
}

}  // namespace

namespace l4d2 {
double AppliedLookBoost() { return g_applied_boost.load(); }
void NoteMouseLook() { g_last_mouse_ms = NowMs(); }
std::string ModLaunchOptions() { return REXCVAR_GET(l4d2_mod_launch_options); }
void StartConsoleBridge() {
  if (g_running.exchange(true)) return;
  g_thread = std::thread(Worker);
}
void StopConsoleBridge() {
  if (!g_running.exchange(false)) return;
  if (g_thread.joinable()) g_thread.join();
}
}  // namespace l4d2

// Hook side: called on the game thread that polls the pad.
extern "C" __declspec(dllexport) void L4d2SetGuestBase(uint8_t* base) { g_base = base; }

extern "C" __declspec(dllexport) int L4d2NextConsoleCommand(char* out, int cap) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_queue2.empty() || cap < 2) return 0;
  std::string c = g_queue2.front().cmd;
  // Boost changes count as applied when the hook takes them; Cbuf runs them
  // within the same frame.
  if (g_queue2.front().boost > 0) g_applied_boost = g_queue2.front().boost;
  g_queue2.pop_front();
  if (int(c.size()) > cap - 2) c.resize(cap - 2);
  memcpy(out, c.data(), c.size());
  out[c.size()] = '\n';  // Cbuf_AddText expects complete lines
  out[c.size() + 1] = 0;
  return 1;
}

extern "C" __declspec(dllexport) void L4d2ConsoleLog(const char* msg) { Log("%s", msg); }
