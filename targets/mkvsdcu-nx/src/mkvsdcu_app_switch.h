#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/ui/overlay/debug_overlay.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class MkvsdcuAppSwitch : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  ~MkvsdcuAppSwitch() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx);

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override;
  void OnPostInitLogging() override;
  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnPostSetup() override;
  void OnShutdown() override;

 private:
  void SetupContentPaths();
  static void SetCVarDefault(const char* name, const char* value);

  rex::ui::FrameStats SampleFrameStats();
  void StartHangWatchdog();
  void StopHangWatchdog();
  void HangWatchdogMain();

  template <typename ThreadList>
  static void DumpThreads(const ThreadList& threads, bool is_error);

  std::atomic<bool> watchdog_active_{false};
  std::thread watchdog_thread_;

  // Frame telemetry
  rex::ui::FrameStats frame_stats_{};
  std::chrono::steady_clock::time_point last_sample_time_{};
  uint64_t last_frame_count_{0};
  double smooth_frame_ms_{0.0};
  bool has_previous_sample_{false};
};
