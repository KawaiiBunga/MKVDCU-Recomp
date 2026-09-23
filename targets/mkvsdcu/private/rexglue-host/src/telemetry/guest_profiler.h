#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Sampling profiler for the game's busiest threads. It pauses each thread for
// a moment, reads where it is executing, and maps that back to a recompiled
// guest function (sub_XXXXXXXX) or a runtime/system symbol. Used to find
// spin-waits and hot guest code worth replacing with native implementations.
namespace telemetry {

struct ProfileEntry {
  std::string location;  // "guest sub_82345678" or "rexruntime.dll!Symbol"
  uint32_t samples = 0;
  double percent = 0;
};

struct ThreadProfile {
  uint32_t thread_id = 0;
  std::string thread_name;
  uint32_t samples = 0;
  std::vector<ProfileEntry> top;  // most samples first
};

struct ProfileReport {
  bool running = false;
  std::string summary;  // one line, or an error
  std::filesystem::path file;
  std::vector<ThreadProfile> threads;
};

class GuestProfiler {
 public:
  ~GuestProfiler();

  // Samples the given threads (usually the busiest ones from SystemSampler)
  // for `seconds` on a worker thread and writes a report to `directory`.
  void Start(std::vector<std::pair<uint32_t, std::string>> threads, double seconds,
             std::filesystem::path directory);
  ProfileReport Report() const;

 private:
  void Run(std::vector<std::pair<uint32_t, std::string>> threads, double seconds,
           std::filesystem::path directory);

  mutable std::mutex mutex_;
  ProfileReport report_;
  std::thread worker_;
};

}  // namespace telemetry
