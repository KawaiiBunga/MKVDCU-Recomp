#include "mkvsdcu_app_switch.h"
#include "generated/default/mkvsdcu_init.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include <algorithm>
#include <cinttypes>
#include <cstring>

std::atomic<uint64_t> g_mkvsdcu_frames_rendered{0};

MkvsdcuAppSwitch::~MkvsdcuAppSwitch() {
  StopHangWatchdog();
}

std::unique_ptr<rex::ui::WindowedApp> MkvsdcuAppSwitch::Create(
    rex::ui::WindowedAppContext& ctx) {
  return std::unique_ptr<MkvsdcuAppSwitch>(
      new MkvsdcuAppSwitch(ctx, "mkvsdcu", PPCImageConfig));
}

void MkvsdcuAppSwitch::SetCVarDefault(const char* name, const char* value) {
  if (rex::cvar::GetFlagInfo(name) == nullptr) {
    return;
  }
  if (rex::cvar::HasNonDefaultValue(name)) {
    return;  // Explicit user override via command line or config file
  }
  rex::cvar::SetFlagByName(name, value);
}

void MkvsdcuAppSwitch::SetupContentPaths() {
  if (rex::cvar::GetFlagInfo("content_backup_root") != nullptr) {
    if (rex::cvar::GetFlagByName("content_backup_root").empty()) {
      const auto exe_dir = rex::filesystem::GetExecutableFolder();
      if (!exe_dir.empty()) {
        const auto save_dir = exe_dir / "saves";
        rex::cvar::SetFlagByName("content_backup_root", rex::path_to_utf8(save_dir));
        REXLOG_INFO("[mkvsdcu] Saves directed to: {}", rex::path_to_utf8(save_dir));
      }
    }
  }
}

void MkvsdcuAppSwitch::OnConfigurePaths(rex::PathConfig& paths) {
  SetupContentPaths();

  if (!paths.game_data_root.empty()) {
    return;
  }

  std::error_code ec;
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  if (exe_dir.empty() || !std::filesystem::is_directory(exe_dir, ec)) {
    return;
  }

  // 1. Look for game_root folder next to NRO (standard Switch homebrew layout: sdmc:/switch/mkvsdcu/game_root)
  const auto game_root = exe_dir / "game_root";
  if (std::filesystem::is_directory(game_root, ec)) {
    paths.game_data_root = game_root;
    REXLOG_INFO("[mkvsdcu] Using game_root at: {}", rex::path_to_utf8(game_root));
    return;
  }

  // 2. Look for any .iso file in the executable directory
  for (const auto& entry : std::filesystem::directory_iterator(exe_dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    if (entry.path().extension() == ".iso" || entry.path().extension() == ".ISO") {
      paths.game_data_root = entry.path();
      REXLOG_INFO("[mkvsdcu] Found game ISO at: {}", rex::path_to_utf8(entry.path()));
      return;
    }
  }
}

#if defined(__SWITCH__) || defined(REX_PLATFORM_SWITCH)
// Override libnx's 8 MB weak nvMap bookkeeping area to 32 MB to prevent
// 0x235C = LibnxNvidiaError_SharedMemoryTooSmall when allocating GPU handles for textures & render targets.
extern "C" uint32_t __nx_nv_transfermem_size = 32 * 1024 * 1024;
#endif

void MkvsdcuAppSwitch::OnPostInitLogging() {
#if defined(__SWITCH__) || defined(REX_PLATFORM_SWITCH)
  __nx_nv_transfermem_size = 32 * 1024 * 1024;
  REXLOG_INFO("[mkvsdcu] Horizon nvMap transfer memory size set to {} MB", __nx_nv_transfermem_size >> 20);
#endif

  // Always enforce Xenos GPU emulation plugin on Switch
  SetCVarDefault("gpu_plugin", "xenos");
  SetCVarDefault("mnk_mode", "false");
  SetCVarDefault("gpu_frame_rate_limit", "60");

  // Title-specific Vulkan rendering configuration for Nintendo Switch / NVK:
  // Must be set here before LoadGpuPlugin() in OnPreSetup():
  // 1. Exact EDRAM depth/stencil emulation: FBO omits fighting surfaces.
  SetCVarDefault("render_target_path_vulkan", "fsi");
  // 2. Direct host resolves remain available if FBO is explicitly selected.
  SetCVarDefault("direct_host_resolve", "true");
  // 3. Disable MSAA on Switch: MSAA hangs NVK driver and wastes bandwidth/fillrate
  SetCVarDefault("native_2x_msaa", "false");
  // 4. Use the render-pass path verified with FSI, and primary buffer submit.
  SetCVarDefault("vulkan_dynamic_rendering", "false");
  SetCVarDefault("vulkan_submit_on_primary_buffer_end", "true");
  SetCVarDefault("vulkan_async_skip_incomplete_frames", "true");

  // Avoid CPU pipeline stalls from GPU readbacks
  SetCVarDefault("readback_resolve", "none");
  SetCVarDefault("vulkan_readback_resolve", "false");
  SetCVarDefault("depth_float24_round", "true");
  SetCVarDefault("shared_memory", "true");
}

void MkvsdcuAppSwitch::OnPreSetup(rex::RuntimeConfig& config) {
  const std::string plugin = rex::cvar::GetFlagByName("gpu_plugin");
  if (!config.graphics && !plugin.empty()) {
    config.graphics = rex::system::LoadGpuPlugin(plugin, "vulkan");
  }
}

void MkvsdcuAppSwitch::OnPostSetup() {
  // Guest frame stats telemetry for overlay
  SetGuestFrameStats([this] { return SampleFrameStats(); });

  // Start autonomous hang watchdog
  StartHangWatchdog();

  REXLOG_INFO("[mkvsdcu] Switch runtime initialized with active diagnostics and telemetry.");
}

void MkvsdcuAppSwitch::OnShutdown() {
  StopHangWatchdog();
}

rex::ui::FrameStats MkvsdcuAppSwitch::SampleFrameStats() {
  using Clock = std::chrono::steady_clock;
  const auto now = Clock::now();
  const uint64_t total = g_mkvsdcu_frames_rendered.load(std::memory_order_relaxed);
  frame_stats_.frame_count = total > 0 ? total : 1;

  if (!has_previous_sample_) {
    has_previous_sample_ = true;
    last_sample_time_ = now;
    last_frame_count_ = total;
    return frame_stats_;
  }

  const double dt_ms = std::chrono::duration<double, std::milli>(now - last_sample_time_).count();
  if (dt_ms < 1000.0) {
    return frame_stats_;
  }

  const uint64_t delta_frames = total - last_frame_count_;
  last_sample_time_ = now;
  last_frame_count_ = total;

  if (delta_frames == 0) {
    smooth_frame_ms_ = 0.0;
    frame_stats_.fps = 0.0;
    frame_stats_.frame_time_ms = dt_ms;
    return frame_stats_;
  }

  const double ms_per_frame = dt_ms / static_cast<double>(delta_frames);
  smooth_frame_ms_ = (smooth_frame_ms_ <= 0.0 || dt_ms > 5000.0)
                         ? ms_per_frame
                         : (smooth_frame_ms_ * 0.5 + ms_per_frame * 0.5);

  frame_stats_.fps = static_cast<float>(1000.0 / smooth_frame_ms_);
  frame_stats_.frame_time_ms = smooth_frame_ms_;
  return frame_stats_;
}

void MkvsdcuAppSwitch::StartHangWatchdog() {
  watchdog_active_ = true;
  watchdog_thread_ = std::thread([this] { HangWatchdogMain(); });
}

void MkvsdcuAppSwitch::StopHangWatchdog() {
  watchdog_active_ = false;
  if (watchdog_thread_.joinable()) {
    watchdog_thread_.join();
  }
}

template <typename ThreadList>
void MkvsdcuAppSwitch::DumpThreads(const ThreadList& threads, bool is_error) {
  for (const auto& th : threads) {
    const auto* params = th->creation_params();
    auto* state = th->thread_state();
    if (state && state->context()) {
      const auto& ctx = *state->context();
      if (is_error) {
        REXLOG_ERROR("[watchdog] thread id=0x{:X} entry=0x{:08X} main={} running={} | "
                     "lr=0x{:08X} r1=0x{:08X} r13=0x{:08X} r3=0x{:08X} ctr=0x{:08X} last_indirect=0x{:08X}",
                     th->thread_id(), params ? params->start_address : 0, th->main_thread(), th->is_running(),
                     static_cast<uint32_t>(ctx.lr), ctx.r1.u32, ctx.r13.u32, ctx.r3.u32, ctx.ctr.u32,
                     ctx.last_indirect_target);
      } else {
        REXLOG_INFO("[telemetry] thread id=0x{:X} entry=0x{:08X} main={} running={} | "
                    "lr=0x{:08X} r1=0x{:08X} r13=0x{:08X}",
                    th->thread_id(), params ? params->start_address : 0, th->main_thread(), th->is_running(),
                    static_cast<uint32_t>(ctx.lr), ctx.r1.u32, ctx.r13.u32);
      }
    } else {
      REXLOG_DEBUG("[watchdog] thread id=0x{:X} without active context", th->thread_id());
    }
  }
}

void MkvsdcuAppSwitch::HangWatchdogMain() {
  constexpr int kStallSecondsThreshold = 5;
  constexpr int kRoutineSnapshotInterval = 30;

  uint64_t prev_signature = 0;
  int stall_seconds = 0;
  int seconds_since_snapshot = 0;
  bool reported_stall = false;

  while (watchdog_active_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!watchdog_active_) break;

    auto* kernel = rex::system::kernel_state();
    if (!kernel) continue;

    auto threads = kernel->object_table()->GetObjectsByType<rex::system::XThread>();
    if (threads.empty()) continue;

    // Calculate a stable signature sorted by thread ID
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> thread_map;
    for (const auto& th : threads) {
      auto* state = th->thread_state();
      if (state && state->context()) {
        thread_map[th->thread_id()] = {
            static_cast<uint32_t>(state->context()->lr),
            state->context()->r1.u32
        };
      }
    }

    uint64_t signature = 14695981039346656037ULL;
    for (const auto& [tid, regs] : thread_map) {
      signature = (signature ^ tid) * 1099511628211ULL;
      signature = (signature ^ regs.first) * 1099511628211ULL;
      signature = (signature ^ regs.second) * 1099511628211ULL;
    }

    if (signature == prev_signature && !thread_map.empty()) {
      stall_seconds++;
      if (stall_seconds >= kStallSecondsThreshold && !reported_stall) {
        reported_stall = true;
        REXLOG_ERROR("[watchdog] *** POTENTIAL DEADLOCK/STALL DETECTED: No thread register state changed for {}s! ***",
                     stall_seconds);
        DumpThreads(threads, true);
      }
    } else {
      if (reported_stall) {
        REXLOG_INFO("[watchdog] Game resumed execution after {}s stall.", stall_seconds);
        reported_stall = false;
      }
      prev_signature = signature;
      stall_seconds = 0;
    }

    seconds_since_snapshot++;
    if (seconds_since_snapshot >= kRoutineSnapshotInterval) {
      seconds_since_snapshot = 0;
      DumpThreads(threads, false);
    }
  }
}
