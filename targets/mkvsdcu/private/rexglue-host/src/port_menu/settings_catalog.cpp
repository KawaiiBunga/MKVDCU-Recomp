#include "settings_catalog.h"

#include <algorithm>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <rex/cvar.h>

#include "host_tweaks.h"
#include "input/controller_filter.h"

namespace settings_catalog {
namespace {
using Kind = Setting::Kind;
constexpr Apply kLive = Apply::kLive;
constexpr Apply kRestart = Apply::kRestart;

std::string Narrow(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string result(size > 0 ? size - 1 : 0, '\0');
  if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
  return result;
}

// d3d12_adapter counts DXGI adapters in enumeration order.
std::vector<Choice> AdapterChoices() {
  std::vector<Choice> choices = {{"-1", "Automatic"}};
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return choices;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    choices.push_back({std::to_string(i), Narrow(desc.Description)});
  }
  return choices;
}

std::vector<Choice> BackendChoices() {
  std::vector<Choice> choices;
  for (const auto& backend : host_tweaks::AvailableGpuBackends()) {
    choices.push_back({backend, backend == "vulkan" ? "Vulkan" : "Direct3D 12"});
  }
  if (choices.empty()) choices.push_back({"d3d12", "Direct3D 12"});
  return choices;
}

std::vector<Choice> EffectChoices() {
  std::vector<Choice> choices;
  const auto* flag = rex::cvar::GetFlagInfo("present_effect");
  const std::vector<std::string> values =
      flag ? flag->constraints.allowed_values : std::vector<std::string>{"bilinear"};
  for (const auto& value : values) {
    const char* label = value == "cas"    ? "AMD CAS"
                        : value == "fsr"  ? "AMD FSR 1"
                        : value == "fsr2" ? "AMD FSR 2 (experimental)"
                        : value == "fsr3" ? "AMD FSR 3 (experimental)"
                                          : "Bilinear";
    choices.push_back({value, label});
  }
  return choices;
}

BOOL CALLBACK CountMonitor(HMONITOR, HDC, LPRECT, LPARAM data) {
  ++*reinterpret_cast<int*>(data);
  return TRUE;
}

std::vector<Choice> MonitorChoices() {
  int count = 0;
  EnumDisplayMonitors(nullptr, nullptr, CountMonitor, reinterpret_cast<LPARAM>(&count));
  std::vector<Choice> choices = {{"0", "Automatic"}, {"1", "Primary"}};
  for (int i = 2; i <= count; ++i) choices.push_back({std::to_string(i), "Display " + std::to_string(i)});
  return choices;
}

std::vector<Choice> ThreadChoices() {
  std::vector<Choice> choices = {{"-1", "Automatic"}};
  const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  for (unsigned n : {1u, 2u, 4u, 6u, 8u, 12u, 16u}) {
    if (n <= cores) choices.push_back({std::to_string(n), std::to_string(n)});
  }
  return choices;
}

std::vector<Choice> ButtonChoices() {
  std::vector<Choice> choices;
  for (const char* name : port_input::kControlNames) choices.push_back({name, name});
  return choices;
}

bool Is(const ValueOf& value, const char* cvar, std::initializer_list<const char*> values) {
  const std::string current = value(cvar);
  return std::any_of(values.begin(), values.end(), [&](const char* v) { return current == v; });
}

std::vector<Group> Build() {
  std::vector<Group> groups;

  groups.push_back({"DISPLAY", "SCREEN", {
      {"fullscreen", "Mode", "Borderless fullscreen presents as directly as exclusive fullscreen.",
       kLive, Kind::kSegmented, {{"false", "Windowed"}, {"true", "Fullscreen"}}},
      {"window_size", "Window size", "The game's window size, in pixels.", kLive, Kind::kCustom,
       {}, 0, 0, nullptr, [](const ValueOf& v) { return v("fullscreen") != "true"; }},
      {"present_letterbox", "Keep 16:9", "Black bars instead of stretching the picture.", kLive,
       Kind::kBool},
      {"monitor", "Display", "Which monitor the game opens on.", kRestart, Kind::kChoice,
       MonitorChoices()},
  }});

  groups.push_back({"DISPLAY", "RESOLUTION", {
      {"resolution_scale", "Render scale",
       "Renders at a multiple of 720p. Higher is sharper and costs GPU time.", kRestart,
       Kind::kSegmented, {{"1", "1x"}, {"2", "2x"}, {"3", "3x"}}},
      {"present_effect", "Upscaling", "How the picture is scaled to your window.", kRestart,
       Kind::kChoice, EffectChoices()},
      {"present_cas_additional_sharpness", "Sharpness", "Extra sharpening for AMD CAS.", kRestart,
       Kind::kPercent, {}, 0, 100, nullptr,
       [](const ValueOf& v) { return Is(v, "present_effect", {"cas"}); }},
      {"present_fsr_sharpness_reduction", "Softness", "0% is the sharpest FSR output.", kRestart,
       Kind::kPercent, {}, 0, 200, nullptr,
       [](const ValueOf& v) { return Is(v, "present_effect", {"fsr", "fsr2", "fsr3"}); }},
      {"present_fsr_quality_mode", "FSR quality", "Internal resolution FSR 2 and 3 upscale from.",
       kRestart, Kind::kChoice,
       {{"auto", "Automatic"}, {"nativeaa", "Native AA"}, {"quality", "Quality"},
        {"balanced", "Balanced"}, {"performance", "Performance"},
        {"ultra_performance", "Ultra performance"}},
       0, 0, nullptr, [](const ValueOf& v) { return Is(v, "present_effect", {"fsr2", "fsr3"}); }},
  }});

  groups.push_back({"GRAPHICS", "IMAGE", {
      {"swap_post_effect", "Anti-aliasing", "FXAA smooths edges and slightly softens the image.",
       kRestart, Kind::kSegmented,
       {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA extreme"}}},
      {"anisotropic_override", "Texture filtering", "Keeps textures sharp at steep angles.", kLive,
       Kind::kChoice,
       {{"-1", "Game default"}, {"0", "Off"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}}},
      {"async_shader_compilation", "Background shaders",
       "Avoids freezes on new effects; objects may pop in for a frame.", kLive, Kind::kBool},
  }});

  groups.push_back({"GRAPHICS", "RENDERER", {
      {"port_gpu_backend", "Graphics API",
       "Direct3D 12 is recommended. Vulkan stutters until its shader cache fills.", kRestart,
       Kind::kSegmented, BackendChoices()},
      {"d3d12_adapter", "Graphics card", "Which GPU renders the game.", kRestart, Kind::kChoice,
       AdapterChoices(), 0, 0, "d3d12"},
      {"d3d12_allow_variable_refresh_rate_and_tearing", "Allow tearing (VRR)",
       "Lower latency with G-Sync or FreeSync; may tear on other displays.", kRestart,
       Kind::kBool, {}, 0, 0, "d3d12"},
  }});

  groups.push_back({"CONTROLS", "CONTROLLER", {
      {"pad_vibration", "Rumble", nullptr, kLive, Kind::kPercent, {}, 0, 100},
      {"pad_left_deadzone", "Left stick deadzone", "Ignores small movements of a worn stick.",
       kLive, Kind::kPercent, {}, 0, 50},
      {"pad_right_deadzone", "Right stick deadzone", "Ignores small movements of a worn stick.",
       kLive, Kind::kPercent, {}, 0, 50},
      {"pad_trigger_deadzone", "Trigger deadzone", nullptr, kLive, Kind::kPercent, {}, 0, 50},
      {"pad_stick_to_dpad", "Stick moves D-pad", "The left stick also presses the D-pad.", kLive,
       Kind::kBool},
      {"pad_menu_chord", "Back + Start opens menu", "Hold Back and Start to open this menu.",
       kLive, Kind::kBool},
  }});

  {
    Group buttons{"CONTROLS", "BUTTONS", {}};
    static constexpr const char* kLabels[] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT"};
    for (int i = 0; i < 8; ++i) {
      buttons.settings.push_back({port_input::kMapCvars[i], kLabels[i],
                                  "The button the game sees when you press this one.", kLive,
                                  Kind::kChoice, ButtonChoices()});
    }
    buttons.settings.push_back({"reset_mapping", "", nullptr, kLive, Kind::kCustom});
    groups.push_back(std::move(buttons));
  }

  groups.push_back({"CONTROLS", "KEYBOARD", {
      {"mnk_mode", "Keyboard as controller", "Play with the keyboard mapped to Xbox buttons.",
       kLive, Kind::kBool},
      {"key_bindings", "", nullptr, kLive, Kind::kCustom, {}, 0, 0, nullptr,
       [](const ValueOf& v) { return v("mnk_mode") == "true"; }},
  }});

  groups.push_back({"AUDIO", "VOLUME", {
      {"audio_mute", "Mute", nullptr, kLive, Kind::kBool},
      {"port_audio_volume", "Master", nullptr, kLive, Kind::kPercent, {}, 0, 150},
      {"port_audio_center", "Dialogue", "Centre channel level in the stereo mix.", kLive,
       Kind::kPercent, {}, 0, 200},
      {"port_audio_surround", "Surround", "Rear channel level in the stereo mix.", kLive,
       Kind::kPercent, {}, 0, 200},
      {"port_audio_lfe", "Bass", "Subwoofer channel level in the stereo mix.", kLive,
       Kind::kPercent, {}, 0, 100},
  }});
  groups.push_back({"AUDIO", "OUTPUT", {
      {"audio_maxqframes", "Buffer", "Raise this if audio crackles; lower is less latency.",
       kRestart, Kind::kInt, {}, 4, 64},
  }});

  groups.push_back({"PERFORMANCE", "OVERLAY", {
      {"port_perf_overlay", "Show overlay", "F2 toggles it in game.", kLive, Kind::kBool},
      {"port_perf_detail", "Details", "CPU, GPU, memory and what limits the frame rate.", kLive,
       Kind::kBool, {}, 0, 0, nullptr,
       [](const ValueOf& v) { return v("port_perf_overlay") == "true"; }},
      {"port_perf_corner", "Position", nullptr, kLive, Kind::kChoice,
       {{"0", "Top left"}, {"1", "Top right"}, {"2", "Bottom left"}, {"3", "Bottom right"}}, 0, 0,
       nullptr, [](const ValueOf& v) { return v("port_perf_overlay") == "true"; }},
      {"csv_log", "Log to file", "Writes one line of these measurements per second.", kLive,
       Kind::kCustom},
  }});

  groups.push_back({"PERFORMANCE", "FRAME RATE", {
      {"vsync", "Game speed",
       "The game is built for 60 Hz. Unlocked runs faster than normal; use it only to test.",
       kLive, Kind::kSegmented, {{"true", "Normal"}, {"false", "Unlocked"}}},
      {"video_mode_refresh_rate", "Game refresh rate",
       "What the game thinks the TV runs at. Anything but 60 Hz changes game speed.", kRestart,
       Kind::kChoice,
       {{"60.000000", "60 Hz"}, {"50.000000", "50 Hz"}, {"120.000000", "120 Hz"},
        {"144.000000", "144 Hz"}}},
      {"profiler", "CPU profiler", "Samples the busiest threads for 5 seconds.", kLive,
       Kind::kCustom},
  }});

  groups.push_back({"ADVANCED", "ACCURACY", {
      {"native_2x_msaa", "Native 2x MSAA", "Uses the GPU's own multisampling where the game asks.",
       kRestart, Kind::kBool},
      {"gamma_render_target_as_unorm16", "16-bit gamma targets", "More accurate gamma colours.",
       kRestart, Kind::kBool},
      {"half_pixel_offset", "Half-pixel offset", "Matches Direct3D 9 pixel centres.", kRestart,
       Kind::kBool},
      {"occlusion_query_enable", "Occlusion queries", "Off may break lens flares and culling.",
       kLive, Kind::kBool},
      {"readback_resolve", "Resolve readback", "Copies render results back to the CPU.", kLive,
       Kind::kChoice,
       {{"none", "Off"}, {"some", "Occasional"}, {"fast", "Fast"}, {"full", "Full"}}},
      {"readback_memexport", "Memexport readback", "Keeps CPU memory in sync with GPU writes.",
       kLive, Kind::kBool},
      {"clear_memory_page_state", "Memory coherency", "Off can show stale data.", kLive,
       Kind::kBool},
  }});

  groups.push_back({"ADVANCED", "RENDERER", {
      {"render_target_path_d3d12", "Render targets", "Rasterizer-ordered views are exact but slow.",
       kRestart, Kind::kChoice,
       {{"", "Automatic"}, {"rtv", "Host (fast)"}, {"rov", "ROV (accurate)"}}, 0, 0, "d3d12"},
      {"render_target_path_vulkan", "Render targets", "Interlock is exact but slow.", kRestart,
       Kind::kChoice, {{"", "Automatic"}, {"fbo", "Host (fast)"}, {"fsi", "Interlock (accurate)"}},
       0, 0, "vulkan"},
      {"d3d12_bindless", "Bindless resources", "Turn off only to work around a driver bug.",
       kRestart, Kind::kBool, {}, 0, 0, "d3d12"},
      {"d3d12_queue_priority", "GPU priority", nullptr, kRestart, Kind::kChoice,
       {{"0", "Normal"}, {"1", "High"}, {"2", "Realtime"}}, 0, 0, "d3d12"},
      {"d3d12_pipeline_creation_threads", "Shader threads", nullptr, kRestart, Kind::kChoice,
       ThreadChoices(), 0, 0, "d3d12"},
      {"store_shaders", "Shader cache", "Keeps compiled shaders so stutter happens only once.",
       kRestart, Kind::kBool},
      {"present_dither", "Dithering", "Hides colour banding in dark gradients.", kRestart,
       Kind::kBool},
      {"present_allow_overscan_cutoff", "Crop overscan", "Trims edges a TV would hide.", kRestart,
       Kind::kBool},
  }});

  groups.push_back({"ADVANCED", "MEMORY", {
      {"texture_cache_memory_limit_soft", "Texture cache (MB)",
       "Unused textures above this are released after the lifetime below.", kRestart, Kind::kInt,
       {}, 64, 4096},
      {"texture_cache_memory_limit_hard", "Texture cache limit (MB)",
       "Textures above this are released at once.", kRestart, Kind::kInt, {}, 128, 8192},
      {"texture_cache_memory_limit_soft_lifetime", "Texture lifetime (s)", nullptr, kLive,
       Kind::kInt, {}, 1, 300},
  }});

  groups.push_back({"ADVANCED", "SYSTEM", {
      {"port_process_priority", "Process priority", nullptr, kLive, Kind::kChoice,
       {{"normal", "Normal"}, {"above_normal", "Above normal"}, {"high", "High"}}},
      {"port_timer_resolution", "1 ms timer", "May smooth pacing on some PCs.", kLive, Kind::kBool},
      {"port_power_throttling", "Power throttling", "Off keeps Windows from slowing the game.",
       kLive, Kind::kBool},
      {"reset_advanced", "", nullptr, kLive, Kind::kCustom},
  }});

  return groups;
}
}  // namespace

const std::vector<Group>& Groups() {
  static const std::vector<Group> groups = Build();
  return groups;
}

namespace {
std::vector<const char*> CvarsWith(Apply apply) {
  std::vector<const char*> names;
  for (const auto& group : Groups()) {
    for (const auto& setting : group.settings) {
      if (setting.kind != Setting::Kind::kCustom && setting.apply == apply) {
        names.push_back(setting.cvar);
      }
    }
  }
  return names;
}
}  // namespace

std::vector<const char*> LiveCvars() {
  return CvarsWith(Apply::kLive);
}
std::vector<const char*> RestartCvars() {
  return CvarsWith(Apply::kRestart);
}

}  // namespace settings_catalog
