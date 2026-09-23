#include "host_tweaks.h"

#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>

#include <rex/audio/downmix.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(port_gpu_backend, "d3d12", "Port/System",
                      "Graphics backend: d3d12 or vulkan")
    .allowed({"d3d12", "vulkan"})
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
