// L4D2 PC port - built-in sampling profiler (developer tool).
//
// l4d2_profile=<start>:<seconds> (e.g. "150:30") samples every thread of the
// process ~500 times a second for <seconds>, starting <start> seconds after
// launch, then writes logs\profile.txt:
//   * per thread: CPU time used, share of samples, its top functions
//   * overall top functions (self time) across all threads
// Addresses are resolved with DbgHelp from the .pdb files next to the DLLs
// (recompiled guest functions show up as sub_XXXXXXXX).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>

#pragma comment(lib, "dbghelp.lib")

REXCVAR_DEFINE_STRING(l4d2_profile, "", "L4D2 Debug",
                      "Sampling profiler: 'start_seconds:duration_seconds' -> logs\\profile.txt");

namespace {

std::atomic<bool> g_running{false};
std::thread g_thread;

struct ThreadInfo {
  std::string name;
  uint64_t samples = 0;
  uint64_t cpu_start = 0, cpu_end = 0;  // 100 ns units
  std::unordered_map<uint64_t, uint64_t> rips;
};

uint64_t ThreadCpu(HANDLE h) {
  FILETIME c, e, k, u;
  if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
  return (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}

std::string ThreadName(HANDLE h, DWORD tid) {
  using GetDesc = HRESULT(WINAPI*)(HANDLE, PWSTR*);
  static GetDesc fn = reinterpret_cast<GetDesc>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
  std::string out;
  PWSTR w = nullptr;
  if (fn && SUCCEEDED(fn(h, &w)) && w) {
    char buf[256];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), nullptr, nullptr);
    out = buf;
    LocalFree(w);
  }
  if (out.empty()) out = "thread " + std::to_string(tid);
  return out;
}

std::string Symbol(uint64_t rip) {
  static std::unordered_map<uint64_t, std::string> cache;
  auto it = cache.find(rip);
  if (it != cache.end()) return it->second;
  char buf[sizeof(SYMBOL_INFO) + 512];
  auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
  si->SizeOfStruct = sizeof(SYMBOL_INFO);
  si->MaxNameLen = 500;
  DWORD64 disp = 0;
  std::string name;
  if (SymFromAddr(GetCurrentProcess(), rip, &disp, si)) {
    name = si->Name;
  } else {
    char mod[MAX_PATH] = "?";
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(rip), &hm)) {
      GetModuleFileNameA(hm, mod, sizeof(mod));
      char b[64];
      snprintf(b, sizeof(b), "+0x%llX", (unsigned long long)(rip - uint64_t(hm)));
      name = std::filesystem::path(mod).filename().string() + b;
    } else {
      name = "?";
    }
  }
  // Module prefix makes recompiled module functions easy to tell apart.
  HMODULE hm = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(rip), &hm)) {
    char mod[MAX_PATH];
    GetModuleFileNameA(hm, mod, sizeof(mod));
    name = std::filesystem::path(mod).stem().string() + "!" + name;
  }
  cache[rip] = name;
  return name;
}

void Worker(double start_s, double dur_s) {
  const auto t0 = std::chrono::steady_clock::now();
  while (g_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < start_s) Sleep(200);
  if (!g_running) return;
  const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
  std::map<DWORD, ThreadInfo> threads;
  std::map<DWORD, HANDLE> handles;
  const auto s0 = std::chrono::steady_clock::now();
  auto last_enum = s0 - std::chrono::seconds(10);
  uint64_t rounds = 0;
  while (g_running && std::chrono::duration<double>(std::chrono::steady_clock::now() - s0).count() < dur_s) {
    if (std::chrono::steady_clock::now() - last_enum > std::chrono::seconds(1)) {
      last_enum = std::chrono::steady_clock::now();
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      THREADENTRY32 te{sizeof(te)};
      for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || handles.count(te.th32ThreadID)) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        handles[te.th32ThreadID] = h;
        auto& ti = threads[te.th32ThreadID];
        ti.name = ThreadName(h, te.th32ThreadID);
        ti.cpu_start = ThreadCpu(h);
      }
      CloseHandle(snap);
    }
    for (auto& [tid, h] : handles) {
      if (SuspendThread(h) == DWORD(-1)) continue;
      // No allocation while the thread is suspended: it may hold the heap lock.
      CONTEXT ctx{};
      ctx.ContextFlags = CONTEXT_CONTROL;
      const bool ok = GetThreadContext(h, &ctx) != 0;
      ResumeThread(h);
      if (ok) {
        auto& ti = threads[tid];
        ti.rips[ctx.Rip]++;
        ti.samples++;
      }
    }
    ++rounds;
    Sleep(2);
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - s0).count();
  for (auto& [tid, h] : handles) {
    threads[tid].cpu_end = ThreadCpu(h);
  }
  // Resolve and write.
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  const std::string exe_dir = rex::filesystem::GetExecutableFolder().string();
  SymInitialize(GetCurrentProcess(), exe_dir.c_str(), TRUE);
  const auto out_path = rex::filesystem::GetExecutableFolder() / "logs" / "profile.txt";
  FILE* f = _wfopen(out_path.c_str(), L"w");
  if (f) {
    fprintf(f, "Sampling profile: %.1f s, %llu rounds (~%.0f samples/s per thread)\n\n", secs,
            (unsigned long long)rounds, rounds / secs);
    std::vector<std::pair<double, DWORD>> order;
    for (auto& [tid, ti] : threads) order.push_back({double(ti.cpu_end - ti.cpu_start) / 1e7 / secs, tid});
    std::sort(order.rbegin(), order.rend());
    fprintf(f, "Threads by CPU use (100%% = one full core):\n");
    for (auto& [cpu, tid] : order) {
      if (cpu < 0.02) continue;
      fprintf(f, "  %5.1f%%  %-40s (tid %lu)\n", cpu * 100, threads[tid].name.c_str(), tid);
    }
    std::unordered_map<std::string, double> overall;  // symbol -> core-seconds estimate
    int shown = 0;
    for (auto& [cpu, tid] : order) {
      auto& ti = threads[tid];
      if (cpu < 0.05 || ti.samples == 0) continue;
      std::unordered_map<std::string, uint64_t> by_sym;
      for (auto& [rip, n] : ti.rips) by_sym[Symbol(rip)] += n;
      std::vector<std::pair<uint64_t, std::string>> v;
      for (auto& [s, n] : by_sym) {
        v.push_back({n, s});
        overall[s] += cpu * double(n) / double(ti.samples);
      }
      std::sort(v.rbegin(), v.rend());
      if (shown++ < 8) {
        fprintf(f, "\n== %s  (%.1f%% CPU, %llu samples)\n", ti.name.c_str(), cpu * 100, (unsigned long long)ti.samples);
        for (size_t i = 0; i < v.size() && i < 30; ++i)
          fprintf(f, "  %5.1f%%  %s\n", 100.0 * v[i].first / ti.samples, v[i].second.c_str());
      }
    }
    std::vector<std::pair<double, std::string>> ov;
    for (auto& [s, c] : overall) ov.push_back({c, s});
    std::sort(ov.rbegin(), ov.rend());
    fprintf(f, "\n== Overall (share of one core)\n");
    for (size_t i = 0; i < ov.size() && i < 60; ++i) fprintf(f, "  %5.1f%%  %s\n", ov[i].first * 100, ov[i].second.c_str());
    fclose(f);
  }
  SymCleanup(GetCurrentProcess());
  for (auto& [tid, h] : handles) CloseHandle(h);
}

}  // namespace

namespace l4d2 {
void StartProfiler() {
  const std::string spec = REXCVAR_GET(l4d2_profile);
  const size_t c = spec.find(':');
  if (spec.empty() || c == std::string::npos || g_running.exchange(true)) return;
  g_thread = std::thread(Worker, atof(spec.substr(0, c).c_str()), std::max(1.0, atof(spec.substr(c + 1).c_str())));
}
void StopProfiler() {
  if (!g_running.exchange(false)) return;
  if (g_thread.joinable()) g_thread.join();
}
}  // namespace l4d2
