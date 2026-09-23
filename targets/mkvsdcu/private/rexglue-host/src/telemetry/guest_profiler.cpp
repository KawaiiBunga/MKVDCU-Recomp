#include "guest_profiler.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <rex/ppc/func.h>

namespace telemetry {
namespace {

// Recompiled functions sorted by their host address, so an instruction
// pointer inside generated code can be traced back to the guest function.
struct HostFunction {
  uintptr_t host;
  uint32_t guest;
};

const std::vector<HostFunction>& HostFunctions() {
  static const std::vector<HostFunction> functions = [] {
    std::vector<HostFunction> result;
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m) {
      result.push_back({reinterpret_cast<uintptr_t>(m->host), uint32_t(m->guest)});
    }
    std::sort(result.begin(), result.end(),
              [](const HostFunction& a, const HostFunction& b) { return a.host < b.host; });
    return result;
  }();
  return functions;
}

std::string Describe(uintptr_t address, uintptr_t exe_begin, uintptr_t exe_end) {
  char text[256];
  if (address >= exe_begin && address < exe_end) {
    const auto& functions = HostFunctions();
    auto it = std::upper_bound(functions.begin(), functions.end(), address,
                               [](uintptr_t a, const HostFunction& f) { return a < f.host; });
    if (it != functions.begin()) {
      --it;
      // Beyond the last generated function is host code in the exe.
      if (std::next(it) != functions.end() || address - it->host < 0x10000) {
        std::snprintf(text, sizeof(text), "guest sub_%08X", it->guest);
        return text;
      }
    }
  }
  HMODULE module = nullptr;
  char module_name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(address), &module)) {
    char path[MAX_PATH];
    if (GetModuleFileNameA(module, path, MAX_PATH)) {
      const char* slash = std::strrchr(path, '\\');
      std::snprintf(module_name, sizeof(module_name), "%s", slash ? slash + 1 : path);
    }
  }
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  DWORD64 displacement = 0;
  if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
    std::snprintf(text, sizeof(text), "%s!%s", module_name, symbol->Name);
  } else if (module) {
    std::snprintf(text, sizeof(text), "%s+0x%llX", module_name,
                  static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(module)));
  } else {
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(address));
  }
  return text;
}
}  // namespace

GuestProfiler::~GuestProfiler() {
  if (worker_.joinable()) worker_.join();
}

void GuestProfiler::Start(std::vector<std::pair<uint32_t, std::string>> threads, double seconds,
                          std::filesystem::path directory) {
  {
    std::lock_guard lock(mutex_);
    if (report_.running) return;
    report_ = {};
    report_.running = true;
    report_.summary = "Profiling...";
  }
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, threads = std::move(threads), seconds, directory]() mutable {
    Run(std::move(threads), seconds, std::move(directory));
  });
}

ProfileReport GuestProfiler::Report() const {
  std::lock_guard lock(mutex_);
  return report_;
}

void GuestProfiler::Run(std::vector<std::pair<uint32_t, std::string>> threads, double seconds,
                        std::filesystem::path directory) {
  SetThreadDescription(GetCurrentThread(), L"Port profiler");
  struct Target {
    uint32_t id;
    std::string name;
    HANDLE handle;
    std::vector<uintptr_t> samples;
  };
  std::vector<Target> targets;
  for (auto& [id, name] : threads) {
    if (id == GetCurrentThreadId()) continue;
    HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id);
    if (handle) targets.push_back({id, std::move(name), handle, {}});
  }
  if (targets.empty()) {
    std::lock_guard lock(mutex_);
    report_.running = false;
    report_.summary = "No threads to profile yet.";
    return;
  }

  // Only read the context while a thread is suspended: it may hold a heap or
  // loader lock, so allocating or symbolizing before resuming could deadlock.
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  for (auto& target : targets) target.samples.reserve(size_t(seconds * 1000) + 16);
  while (std::chrono::steady_clock::now() < end) {
    for (auto& target : targets) {
      if (SuspendThread(target.handle) == DWORD(-1)) continue;
      CONTEXT context = {};
      context.ContextFlags = CONTEXT_CONTROL;
      const bool ok = GetThreadContext(target.handle, &context);
      ResumeThread(target.handle);
      if (ok && target.samples.size() < target.samples.capacity()) target.samples.push_back(context.Rip);
    }
    Sleep(1);
  }

  MODULEINFO exe = {};
  GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &exe, sizeof(exe));
  const auto exe_begin = reinterpret_cast<uintptr_t>(exe.lpBaseOfDll);
  const auto exe_end = exe_begin + exe.SizeOfImage;
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  const bool symbols = SymInitialize(GetCurrentProcess(), nullptr, TRUE);

  ProfileReport report;
  uint32_t total = 0;
  for (auto& target : targets) {
    CloseHandle(target.handle);
    std::unordered_map<uintptr_t, uint32_t> by_address;
    for (uintptr_t rip : target.samples) ++by_address[rip];
    std::map<std::string, uint32_t> by_location;
    for (const auto& [rip, count] : by_address) by_location[Describe(rip, exe_begin, exe_end)] += count;

    ThreadProfile profile;
    profile.thread_id = target.id;
    profile.thread_name = target.name;
    profile.samples = uint32_t(target.samples.size());
    total += profile.samples;
    for (const auto& [location, count] : by_location) {
      profile.top.push_back({location, count, profile.samples ? 100.0 * count / profile.samples : 0});
    }
    std::sort(profile.top.begin(), profile.top.end(),
              [](const ProfileEntry& a, const ProfileEntry& b) { return a.samples > b.samples; });
    if (profile.top.size() > 25) profile.top.resize(25);
    report.threads.push_back(std::move(profile));
  }
  if (symbols) SymCleanup(GetCurrentProcess());

  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  const std::time_t now = std::time(nullptr);
  std::tm local = {};
  localtime_s(&local, &now);
  char name[64];
  std::strftime(name, sizeof(name), "profile-%Y%m%d-%H%M%S.txt", &local);
  report.file = directory / name;
  if (std::ofstream out(report.file); out) {
    out << "MKVDCU-Recomp CPU profile, " << seconds << " s, ~1 kHz sampling\n";
    for (const auto& thread : report.threads) {
      out << "\n== " << thread.thread_name << " (" << thread.thread_id << "), " << thread.samples
          << " samples\n";
      for (const auto& entry : thread.top) {
        char line[320];
        std::snprintf(line, sizeof(line), "%6.2f%%  %6u  %s\n", entry.percent, entry.samples,
                      entry.location.c_str());
        out << line;
      }
    }
  }
  report.summary = std::to_string(total) + " samples from " + std::to_string(report.threads.size()) +
                   " threads. Saved " + report.file.filename().string();

  std::lock_guard lock(mutex_);
  report_ = std::move(report);
  report_.running = false;
}

}  // namespace telemetry
