#include "system_telemetry.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include "frame_telemetry.h"

namespace telemetry {
namespace {
using Microsoft::WRL::ComPtr;

uint64_t FileTimeValue(const FILETIME& time) {
  return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

std::string Narrow(const wchar_t* text) {
  if (!text || !*text) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string result(size > 0 ? size - 1 : 0, '\0');
  if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
  return result;
}

using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

std::string ThreadName(HANDLE thread) {
  static const auto get_description = reinterpret_cast<GetThreadDescriptionFn>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
  if (!get_description) return {};
  PWSTR description = nullptr;
  if (FAILED(get_description(thread, &description))) return {};
  std::string name = Narrow(description);
  LocalFree(description);
  return name;
}

std::string CsvField(std::string text) {
  if (text.find_first_of(",\"") == std::string::npos) return text;
  std::string quoted = "\"";
  for (char ch : text) quoted += ch == '"' ? std::string("\"\"") : std::string(1, ch);
  return quoted + "\"";
}
}  // namespace

struct SystemSampler::Impl {
  uint32_t cores = 0;
  std::chrono::steady_clock::time_point last_time;
  uint64_t last_process_time = 0;
  uint64_t last_system_idle = 0, last_system_total = 0;
  uint64_t last_page_faults = 0;
  uint32_t last_vblank = 0;
  uint64_t last_swaps = 0;
  bool primed = false;

  struct ThreadRecord {
    HANDLE handle = nullptr;
    uint64_t cpu_time = 0;
    std::string name;
  };
  std::unordered_map<uint32_t, ThreadRecord> threads;
  uint32_t generation = 0;

  PDH_HQUERY gpu_query = nullptr;
  PDH_HCOUNTER gpu_process = nullptr;
  PDH_HCOUNTER gpu_total = nullptr;
  std::chrono::steady_clock::time_point gpu_query_created;
  double last_gpu_process = -1;
  double last_gpu_total = -1;
  std::string last_gpu_engine;

  ComPtr<IDXGIFactory1> dxgi;

  std::mutex csv_mutex;
  std::ofstream csv;
  std::filesystem::path csv_file;
  std::chrono::steady_clock::time_point csv_start;

  ~Impl() {
    if (gpu_query) PdhCloseQuery(gpu_query);
    for (auto& [id, record] : threads) CloseHandle(record.handle);
  }

  // The engine instances only exist once the process has a D3D device, and
  // PDH does not pick up instances created after the counter was added, so
  // rebuild the query now and then.
  void EnsureGpuQuery(std::chrono::steady_clock::time_point now) {
    if (gpu_query && now - gpu_query_created < std::chrono::seconds(20)) return;
    if (gpu_query) PdhCloseQuery(gpu_query);
    gpu_query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &gpu_query) != ERROR_SUCCESS) return;
    const std::wstring own = L"\\GPU Engine(pid_" + std::to_wstring(GetCurrentProcessId()) +
                             L"_*)\\Utilization Percentage";
    PdhAddEnglishCounterW(gpu_query, own.c_str(), 0, &gpu_process);
    PdhAddEnglishCounterW(gpu_query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpu_total);
    PdhCollectQueryData(gpu_query);
    gpu_query_created = now;
  }

  // Task Manager's figure: add up every process on each engine, then report
  // the busiest engine. D3D12 queues often show up as "graphics_1" rather
  // than "3D", so every engine type counts.
  static double BusiestEngine(PDH_HCOUNTER counter, std::string* engine_out = nullptr) {
    if (!counter) return -1;
    DWORD size = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                     nullptr) != PDH_MORE_DATA) {
      return -1;
    }
    std::vector<uint8_t> buffer(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                     items) != ERROR_SUCCESS) {
      return -1;
    }
    std::map<std::wstring, double> engines;
    for (DWORD i = 0; i < count; ++i) {
      if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA) continue;
      std::wstring name = items[i].szName;
      const size_t luid = name.find(L"luid_");
      if (luid != std::wstring::npos) name = name.substr(luid);
      engines[name] += items[i].FmtValue.doubleValue;
    }
    double busiest = 0;
    for (const auto& [name, value] : engines) {
      if (value > busiest) {
        busiest = value;
        if (engine_out) {
          const size_t type = name.find(L"engtype_");
          *engine_out = Narrow((type == std::wstring::npos ? name : name.substr(type + 8)).c_str());
        }
      }
    }
    return std::min(busiest, 100.0);
  }

  void SampleGpu(SystemSnapshot& out, std::chrono::steady_clock::time_point now) {
    const bool fresh = !gpu_query || now - gpu_query_created >= std::chrono::seconds(20);
    EnsureGpuQuery(now);
    if (gpu_query && !fresh && PdhCollectQueryData(gpu_query) == ERROR_SUCCESS) {
      last_gpu_process = BusiestEngine(gpu_process, &last_gpu_engine);
      last_gpu_total = BusiestEngine(gpu_total);
    }
    // A rebuilt query needs a second collection; show the previous reading.
    out.gpu_3d_percent = last_gpu_process;
    out.gpu_3d_total_percent = last_gpu_total;
    out.gpu_engine = last_gpu_engine;

    if (!dxgi) CreateDXGIFactory1(IID_PPV_ARGS(&dxgi));
    if (!dxgi) return;
    // The adapter this process renders on is the one with its allocations.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; dxgi->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      ComPtr<IDXGIAdapter3> adapter3;
      if (FAILED(adapter.As(&adapter3))) continue;
      DXGI_QUERY_VIDEO_MEMORY_INFO local = {}, shared = {};
      if (FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) {
        continue;
      }
      adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
      if (local.CurrentUsage >= out.vram_used_bytes && local.CurrentUsage > 0) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        out.gpu_name = Narrow(desc.Description);
        out.vram_used_bytes = local.CurrentUsage;
        out.vram_budget_bytes = local.Budget;
        out.shared_used_bytes = shared.CurrentUsage;
      }
    }
  }

  // A Toolhelp snapshot covers every thread on the system and costs a few ms,
  // so only look for new threads every few seconds and keep handles to ours.
  void DiscoverThreads() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 entry = {sizeof(entry)};
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != pid || threads.contains(entry.th32ThreadID)) continue;
      HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ThreadID);
      if (!thread) continue;
      ThreadRecord& record = threads[entry.th32ThreadID];
      record.handle = thread;
      record.cpu_time = ~0ull;  // No baseline yet.
    }
    CloseHandle(snapshot);
  }

  void SampleThreads(SystemSnapshot& out, double elapsed_seconds) {
    if (generation++ % 5 == 0) DiscoverThreads();
    std::vector<ThreadLoad> loads;
    for (auto it = threads.begin(); it != threads.end();) {
      ThreadRecord& record = it->second;
      FILETIME created, exited, kernel, user;
      if (!GetThreadTimes(record.handle, &created, &exited, &kernel, &user) ||
          WaitForSingleObject(record.handle, 0) == WAIT_OBJECT_0) {
        CloseHandle(record.handle);
        it = threads.erase(it);
        continue;
      }
      ++out.thread_count;
      const uint64_t cpu = FileTimeValue(kernel) + FileTimeValue(user);
      // Guest threads are named after they start, so retry until a name sticks.
      if (record.name.empty() || record.name.starts_with("thread ")) {
        record.name = ThreadName(record.handle);
        if (record.name.empty()) record.name = "thread " + std::to_string(it->first);
      }
      if (record.cpu_time != ~0ull && primed && elapsed_seconds > 0) {
        const double seconds = double(cpu - record.cpu_time) / 1e7;
        loads.push_back({it->first, record.name, 100.0 * seconds / elapsed_seconds});
      }
      record.cpu_time = cpu;
      ++it;
    }
    std::sort(loads.begin(), loads.end(),
              [](const ThreadLoad& a, const ThreadLoad& b) { return a.core_percent > b.core_percent; });
    if (loads.size() > 8) loads.resize(8);
    out.busiest_threads = std::move(loads);
  }

  SystemSnapshot Sample(uint32_t vblank) {
    SystemSnapshot out;
    const uint64_t swaps = GuestFrameCount();
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_time).count();
    if (!cores) {
      SYSTEM_INFO info;
      GetSystemInfo(&info);
      cores = info.dwNumberOfProcessors;
    }
    out.logical_cores = cores;

    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const uint64_t process_time = FileTimeValue(kernel) + FileTimeValue(user);
    FILETIME idle_time, kernel_time, user_time;
    GetSystemTimes(&idle_time, &kernel_time, &user_time);
    const uint64_t system_idle = FileTimeValue(idle_time);
    const uint64_t system_total = FileTimeValue(kernel_time) + FileTimeValue(user_time);

    PROCESS_MEMORY_COUNTERS_EX memory = {sizeof(memory)};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                         sizeof(memory));
    out.working_set_bytes = memory.WorkingSetSize;
    out.private_bytes = memory.PrivateUsage;

    SampleThreads(out, elapsed);
    SampleGpu(out, now);

    if (primed && elapsed > 0) {
      out.process_cpu_percent =
          100.0 * double(process_time - last_process_time) / 1e7 / elapsed / cores;
      const uint64_t total = system_total - last_system_total;
      out.system_cpu_percent =
          total ? 100.0 * double(total - (system_idle - last_system_idle)) / double(total) : 0;
      out.page_faults_per_second = double(memory.PageFaultCount - last_page_faults) / elapsed;
      // The SDK's counter also ticks once per swap; remove those.
      const double ticks = double(uint32_t(vblank - last_vblank)) - double(swaps - last_swaps);
      out.vblank_hz = std::max(0.0, ticks) / elapsed;
      out.valid = true;
    }
    last_time = now;
    last_process_time = process_time;
    last_system_idle = system_idle;
    last_system_total = system_total;
    last_page_faults = memory.PageFaultCount;
    last_vblank = vblank;
    last_swaps = swaps;
    primed = true;
    return out;
  }

  void WriteCsv(const SystemSnapshot& s) {
    std::lock_guard lock(csv_mutex);
    if (!csv.is_open() || !s.valid) return;
    const FrameStats f = ComputeFrameStats(1.0);
    const GuestOutputSize size = LastGuestOutputSize();
    const ThreadLoad top = s.busiest_threads.empty() ? ThreadLoad{} : s.busiest_threads.front();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - csv_start).count();
    csv << seconds << ',' << f.fps << ',' << f.avg_ms << ',' << f.p99_ms << ',' << f.max_ms << ','
        << f.stddev_ms << ',' << f.hitches << ',' << size.width << ',' << size.height << ','
        << s.process_cpu_percent << ',' << s.system_cpu_percent << ',' << CsvField(top.name) << ','
        << top.core_percent << ',' << s.gpu_3d_percent << ',' << s.gpu_3d_total_percent << ','
        << s.vram_used_bytes / 1048576 << ',' << s.vram_budget_bytes / 1048576 << ','
        << s.working_set_bytes / 1048576 << ',' << s.private_bytes / 1048576 << ',' << s.vblank_hz
        << '\n';
    csv.flush();
  }
};

SystemSampler::SystemSampler(std::function<uint32_t()> vblank_counter)
    : impl_(std::make_unique<Impl>()), vblank_counter_(std::move(vblank_counter)) {
  thread_ = std::thread([this] { Run(); });
}

SystemSampler::~SystemSampler() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void SystemSampler::SetWanted(const char* reason, bool wanted) {
  {
    std::lock_guard lock(mutex_);
    const auto it = std::find(reasons_.begin(), reasons_.end(), reason);
    if (wanted && it == reasons_.end()) reasons_.push_back(reason);
    if (!wanted && it != reasons_.end()) reasons_.erase(it);
  }
  wake_.notify_all();
}

SystemSnapshot SystemSampler::Latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

std::filesystem::path SystemSampler::SetCsvLogging(bool enabled,
                                                   const std::filesystem::path& directory) {
  {
    std::lock_guard lock(impl_->csv_mutex);
    if (impl_->csv.is_open()) impl_->csv.close();
    impl_->csv_file.clear();
    if (enabled) {
      std::error_code ec;
      std::filesystem::create_directories(directory, ec);
      const std::time_t now = std::time(nullptr);
      std::tm local = {};
      localtime_s(&local, &now);
      char name[64];
      std::strftime(name, sizeof(name), "perf-%Y%m%d-%H%M%S.csv", &local);
      impl_->csv_file = directory / name;
      impl_->csv.open(impl_->csv_file, std::ios::out | std::ios::trunc);
      if (impl_->csv) {
        impl_->csv << "seconds,game_fps,frame_avg_ms,frame_p99_ms,frame_max_ms,frame_stddev_ms,"
                      "hitches,output_width,output_height,process_cpu_pct,system_cpu_pct,"
                      "busiest_thread,busiest_thread_core_pct,gpu_3d_pct,gpu_3d_total_pct,"
                      "vram_used_mb,vram_budget_mb,working_set_mb,private_mb,vblank_hz\n";
        impl_->csv_start = std::chrono::steady_clock::now();
      } else {
        impl_->csv_file.clear();
      }
    }
  }
  SetWanted("csv", enabled);
  return csv_path();
}

std::filesystem::path SystemSampler::csv_path() const {
  std::lock_guard lock(impl_->csv_mutex);
  return impl_->csv_file;
}

void SystemSampler::Run() {
  SetThreadDescription(GetCurrentThread(), L"Port telemetry");
  std::unique_lock lock(mutex_);
  while (!stop_) {
    if (reasons_.empty()) {
      impl_->primed = false;
      latest_ = {};
      wake_.wait(lock, [this] { return stop_ || !reasons_.empty(); });
      continue;
    }
    lock.unlock();
    const uint32_t vblank = vblank_counter_ ? vblank_counter_() : 0;
    SystemSnapshot snapshot = impl_->Sample(vblank);
    impl_->WriteCsv(snapshot);
    lock.lock();
    if (snapshot.valid || !latest_.valid) latest_ = std::move(snapshot);
    wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; });
  }
}

}  // namespace telemetry
