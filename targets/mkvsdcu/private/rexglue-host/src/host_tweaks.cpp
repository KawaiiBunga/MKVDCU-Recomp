#include "host_tweaks.h"

#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>

#include <rex/audio/downmix.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(port_gpu_backend, "d3d12", "Port/System",
                      "Graphics backend: d3d12 or vulkan")
    .allowed({"d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(port_render_mode, "legacy", "Port/Graphics",
                      "Internal render scale: legacy, native, wide_2x, tall_2x, 2x or 3x")
    .allowed({"legacy", "native", "wide_2x", "tall_2x", "2x", "3x"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(port_timer_resolution, true, "Port/System",
                    "Ask Windows for 1 ms timer resolution so the guest vblank thread wakes "
                    "on time");
REXCVAR_DEFINE_STRING(port_process_priority, "above_normal", "Port/System",
                      "Process priority: normal, above_normal or high")
    .allowed({"normal", "above_normal", "high"});
REXCVAR_DEFINE_BOOL(port_power_throttling, false, "Port/System",
                    "Let Windows power-throttle the game (EcoQoS)");
REXCVAR_DEFINE_INT32(port_audio_volume, 100, "Port/Audio", "Master volume in percent")
    .range(0, 150);
REXCVAR_DEFINE_INT32(port_audio_center, 100, "Port/Audio",
                     "Dialogue (centre channel) level in the stereo mix, percent")
    .range(0, 200);
REXCVAR_DEFINE_INT32(port_audio_surround, 100, "Port/Audio",
                     "Surround channel level in the stereo mix, percent")
    .range(0, 200);
REXCVAR_DEFINE_INT32(port_audio_lfe, 0, "Port/Audio", "Subwoofer (LFE) level in the stereo mix, percent")
    .range(0, 100);

namespace host_tweaks {
void ApplyRenderMode() {
  const std::string mode = REXCVAR_GET(port_render_mode);
  if (mode == "legacy") return;

  // Explicit SDK scale launch flags are useful for benchmarking and take
  // precedence over the saved menu choice.
  const auto mode_source = rex::cvar::GetFlagSource("port_render_mode");
  if (mode_source != rex::cvar::Source::kCommandLine &&
      mode_source != rex::cvar::Source::kEnvironment) {
    for (const char* name : {"resolution_scale", "draw_resolution_scale_x",
                             "draw_resolution_scale_y"}) {
      const auto source = rex::cvar::GetFlagSource(name);
      if (source == rex::cvar::Source::kCommandLine ||
          source == rex::cvar::Source::kEnvironment) return;
    }
  }

  const int x = mode == "wide_2x" || mode == "2x" ? 2 : mode == "3x" ? 3 : 1;
  const int y = mode == "tall_2x" || mode == "2x" ? 2 : mode == "3x" ? 3 : 1;
  // The SDK's shared scale overrides an axis left at its default. Clear it
  // before setting the independent axes, including when migrating a saved 2x.
  const bool applied = rex::cvar::SetFlagByName("resolution_scale", "1") &&
                       rex::cvar::SetFlagByName("draw_resolution_scale_x", std::to_string(x)) &&
                       rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(y));
  if (applied) {
    REXLOG_INFO("Render mode {}: {}x by {}x internal scale", mode, x, y);
  } else {
    REXLOG_WARN("Could not apply render mode {}", mode);
  }
}

std::pair<uint32_t, uint32_t> RenderScale() {
  // Match TextureCache::GetConfigDrawResolutionScale in the pinned SDK.
  const auto clamp = [](int32_t value) { return uint32_t(std::clamp(value, 1, 7)); };
  const uint32_t shared = clamp(rex::cvar::Query<int32_t>("resolution_scale"));
  const bool use_shared = rex::cvar::HasNonDefaultValue("resolution_scale");
  const uint32_t x = use_shared && !rex::cvar::HasNonDefaultValue("draw_resolution_scale_x")
                         ? shared
                         : clamp(rex::cvar::Query<int32_t>("draw_resolution_scale_x"));
  const uint32_t y = use_shared && !rex::cvar::HasNonDefaultValue("draw_resolution_scale_y")
                         ? shared
                         : clamp(rex::cvar::Query<int32_t>("draw_resolution_scale_y"));
  return {x, y};
}

namespace {
constexpr const char* kWatched[] = {"port_timer_resolution", "port_process_priority",
                                    "port_power_throttling", "port_audio_volume",
                                    "port_audio_center",     "port_audio_surround",
                                    "port_audio_lfe"};
bool g_timer_raised = false;

void ApplyTimer() {
  const bool want = REXCVAR_GET(port_timer_resolution);
  if (want && !g_timer_raised) {
    g_timer_raised = timeBeginPeriod(1) == TIMERR_NOERROR;
  } else if (!want && g_timer_raised) {
    timeEndPeriod(1);
    g_timer_raised = false;
  }
}

void ApplyPriority() {
  const std::string& priority = REXCVAR_GET(port_process_priority);
  const DWORD value = priority == "high"           ? HIGH_PRIORITY_CLASS
                      : priority == "above_normal" ? ABOVE_NORMAL_PRIORITY_CLASS
                                                   : NORMAL_PRIORITY_CLASS;
  SetPriorityClass(GetCurrentProcess(), value);
}

void ApplyPowerThrottling() {
  // Execution speed throttling (EcoQoS) and, on Windows 11, ignoring the
  // timer resolution request while the window is covered.
  constexpr ULONG kExecutionSpeed = 0x1, kIgnoreTimerResolution = 0x4;
  PROCESS_POWER_THROTTLING_STATE state = {};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = kExecutionSpeed | kIgnoreTimerResolution;
  state.StateMask = REXCVAR_GET(port_power_throttling) ? state.ControlMask : 0;
  if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) {
    state.ControlMask = kExecutionSpeed;  // older Windows 10 builds
    state.StateMask &= kExecutionSpeed;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
  }
}

void ApplyAudio() {
  rex::audio::SetOutputGain(std::clamp(REXCVAR_GET(port_audio_volume), 0, 150) / 100.0f);
  rex::audio::StereoFold fold;
  fold.center *= std::clamp(REXCVAR_GET(port_audio_center), 0, 200) / 100.0f;
  fold.surround *= std::clamp(REXCVAR_GET(port_audio_surround), 0, 200) / 100.0f;
  fold.lfe = std::clamp(REXCVAR_GET(port_audio_lfe), 0, 100) / 100.0f;
  // Keep the loudest possible sum at the default fold's headroom.
  fold.scale = 1.0f / (1.0f + std::max(fold.center, 0.70710678f) + fold.lfe);
  rex::audio::SetStereoFold(fold);
}

void ApplyAll() {
  ApplyTimer();
  ApplyPriority();
  ApplyPowerThrottling();
  ApplyAudio();
}
}  // namespace

void Install() {
  ApplyAll();
  for (const char* name : kWatched) {
    rex::cvar::RegisterChangeCallback(name, [](std::string_view, std::string_view) { ApplyAll(); });
  }
}

void Uninstall() {
  for (const char* name : kWatched) rex::cvar::UnregisterChangeCallbacks(name);
  if (g_timer_raised) timeEndPeriod(1);
  g_timer_raised = false;
}

std::string GpuBackend() {
  return REXCVAR_GET(port_gpu_backend);
}

std::vector<std::string> AvailableGpuBackends() {
  // Each backend registers its own cvars when the plugin loads.
  std::vector<std::string> backends;
  if (rex::cvar::GetFlagInfo("render_target_path_d3d12")) backends.push_back("d3d12");
  if (rex::cvar::GetFlagInfo("render_target_path_vulkan")) backends.push_back("vulkan");
  return backends;
}

}  // namespace host_tweaks
