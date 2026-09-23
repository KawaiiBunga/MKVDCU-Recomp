#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace telemetry {
class GuestProfiler;
class SystemSampler;
}

struct PortWindowInfo {
  uint32_t width = 0;  // client area, physical pixels
  uint32_t height = 0;
  bool fullscreen = false;
};

// What the port menu and the performance overlay need from the app.
struct PortHost {
  std::filesystem::path config_path;
  std::filesystem::path log_dir;
  telemetry::SystemSampler* sampler = nullptr;
  telemetry::GuestProfiler* profiler = nullptr;
  std::function<PortWindowInfo()> window_info;
  // Resizes the client area of a windowed game window; runs on the UI thread.
  std::function<void(uint32_t width, uint32_t height)> resize_window;
  std::function<void(bool visible)> show_perf_overlay;
  // Returns the CSV file when logging starts, or empty.
  std::function<std::filesystem::path(bool enabled)> set_csv_logging;
};
