#include "settings_catalog.h"

#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <rex/cvar.h>

#include "host_tweaks.h"

namespace settings_catalog {
namespace {
using Apply = settings_catalog::Apply;
using Kind = Setting::Kind;

std::string Narrow(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string result(size > 0 ? size - 1 : 0, '\0');
  if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
  return result;
}

// d3d12_adapter counts DXGI adapters in enumeration order.
std::vector<Choice> AdapterChoices() {
  std::vector<Choice> choices = {{"-1", "Automatic (fastest GPU)"}};
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
    const char* label = value == "cas"    ? "AMD CAS (sharpen)"
                        : value == "fsr"  ? "AMD FSR 1 (upscale + sharpen)"
                        : value == "fsr2" ? "AMD FSR 2 (experimental)"
                        : value == "fsr3" ? "AMD FSR 3 (experimental)"
                                          : "Bilinear";
    choices.push_back({value, label});
  }
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

std::vector<Group> Build() {
  std::vector<Group> groups;

  groups.push_back({"GRAPHICS", "OUTPUT", "How the finished frame reaches the screen.", {
      {"present_effect", "Upscaling filter",
       "Scales the game's frame to the window. CAS sharpens, FSR 1 upscales with edge "
       "reconstruction. FSR 2 and 3 are temporal and experimental here: the presenter has no "
       "motion vectors, so they behave like a sharper spatial filter.",
       Apply::kRestart, Kind::kChoice, EffectChoices()},
      {"present_cas_additional_sharpness", "CAS sharpness", "Extra sharpening for AMD CAS.",
       Apply::kRestart, Kind::kPercent, {}, 0, 100},
      {"present_fsr_sharpness_reduction", "FSR sharpness reduction",
       "0% is the sharpest; higher values soften FSR's sharpening pass.", Apply::kRestart,
       Kind::kPercent, {}, 0, 200},
      {"present_fsr_quality_mode", "FSR 2/3 quality mode", nullptr, Apply::kRestart,
       Kind::kChoice,
       {{"auto", "Automatic"}, {"nativeaa", "Native AA"}, {"quality", "Quality"},
        {"balanced", "Balanced"}, {"performance", "Performance"},
        {"ultra_performance", "Ultra performance"}}},
      {"present_dither", "Output dithering", "Hides colour banding in dark gradients.",
       Apply::kRestart, Kind::kBool},
      {"present_allow_overscan_cutoff", "Crop TV overscan area",
       "Trims the edges the game expects a TV to hide.", Apply::kRestart, Kind::kBool},
  }});

  groups.push_back({"GRAPHICS", "RENDERER", "Graphics API and device.", {
      {"port_gpu_backend", "Graphics API",
       "Direct3D 12 is the tested default. Vulkan can be faster on some AMD and older NVIDIA "
       "drivers.",
       Apply::kRestart, Kind::kChoice, BackendChoices()},
      {"d3d12_adapter", "Graphics card", "Which GPU renders the game.", Apply::kRestart,
       Kind::kChoice, AdapterChoices(), 0, 0, "d3d12"},
      {"render_target_path_d3d12", "Render target emulation",
       "How the Xbox 360's EDRAM is emulated. Host render targets are fast; rasterizer-ordered "
       "views are more exact and much slower on most GPUs.",
       Apply::kRestart, Kind::kChoice,
       {{"", "Automatic"}, {"rtv", "Host render targets (fast)"},
        {"rov", "Rasterizer-ordered views (accurate)"}},
       0, 0, "d3d12"},
      {"render_target_path_vulkan", "Render target emulation", nullptr, Apply::kRestart,
       Kind::kChoice,
       {{"", "Automatic"}, {"fbo", "Host render targets (fast)"},
        {"fsi", "Fragment shader interlock (accurate)"}},
       0, 0, "vulkan"},
      {"d3d12_bindless", "Bindless resources",
       "Fewer descriptor updates per draw. Turn off only to work around driver bugs.",
       Apply::kRestart, Kind::kBool, {}, 0, 0, "d3d12"},
      {"d3d12_queue_priority", "GPU queue priority", "High lets the game's work run ahead of "
       "other programs on the GPU.", Apply::kRestart, Kind::kChoice,
       {{"0", "Normal"}, {"1", "High"}, {"2", "Realtime"}}, 0, 0, "d3d12"},
  }});

  groups.push_back({"GRAPHICS", "ACCURACY", "Emulation details that trade speed for correctness.", {
      {"native_2x_msaa", "Native 2x MSAA", "Uses the GPU's own 2x multisampling where the "
       "game asks for it.", Apply::kRestart, Kind::kBool},
      {"gamma_render_target_as_unorm16", "16-bit gamma render targets",
       "More accurate colours for gamma-space targets at a small bandwidth cost.",
       Apply::kRestart, Kind::kBool},
      {"half_pixel_offset", "Half-pixel offset", "Matches Direct3D 9 pixel centres.",
       Apply::kRestart, Kind::kBool},
      {"occlusion_query_enable", "Occlusion queries",
       "Real visibility tests. Off reports everything visible: faster, may cost lens "
       "flares or culling.", Apply::kLive, Kind::kBool},
      {"readback_resolve", "Render-to-texture readback",
       "Copies GPU results back for the CPU. Needed only if something looks stale.",
       Apply::kLive, Kind::kChoice,
       {{"none", "Off (fastest)"}, {"some", "Occasional"}, {"fast", "Every frame, delayed"},
        {"full", "Every frame, exact (slowest)"}}},
      {"readback_memexport", "Shader memory-export readback",
       "Keeps CPU-visible memory in sync with GPU writes.", Apply::kLive, Kind::kBool},
      {"clear_memory_page_state", "Per-frame memory coherency",
       "Off saves a little CPU but can show stale data.", Apply::kLive, Kind::kBool},
  }});

  groups.push_back({"PERFORMANCE", "SHADERS AND MEMORY", nullptr, {
      {"d3d12_pipeline_creation_threads", "Shader compile threads",
       "Threads that build pipelines in the background.", Apply::kRestart, Kind::kChoice,
       ThreadChoices(), 0, 0, "d3d12"},
      {"store_shaders", "Keep a shader cache",
       "Reuses compiled shaders between launches so stutter only happens once.",
       Apply::kRestart, Kind::kBool},
      {"texture_cache_memory_limit_soft", "Texture cache target (MB)",
       "Above this, unused textures are released after the lifetime below.", Apply::kRestart,
       Kind::kInt, {}, 64, 4096},
      {"texture_cache_memory_limit_hard", "Texture cache limit (MB)",
       "Above this, textures are released immediately.", Apply::kRestart, Kind::kInt, {}, 128,
       8192},
      {"texture_cache_memory_limit_soft_lifetime", "Unused texture lifetime (s)", nullptr,
       Apply::kLive, Kind::kInt, {}, 1, 300},
  }});

  groups.push_back({"AUDIO", "MIX", "Applies immediately.", {
      {"audio_mute", "Mute", nullptr, Apply::kLive, Kind::kBool},
      {"port_audio_volume", "Master volume", nullptr, Apply::kLive, Kind::kPercent, {}, 0, 150},
      {"port_audio_center", "Dialogue level", "Centre channel in the stereo mix.",
       Apply::kLive, Kind::kPercent, {}, 0, 200},
      {"port_audio_surround", "Surround level", "Rear channels folded into stereo.",
       Apply::kLive, Kind::kPercent, {}, 0, 200},
      {"port_audio_lfe", "Bass (LFE) in stereo",
       "The game's subwoofer channel, mixed into the speakers.", Apply::kLive, Kind::kPercent,
       {}, 0, 100},
  }});
  groups.push_back({"AUDIO", "OUTPUT", nullptr, {
      {"audio_maxqframes", "Audio buffer (frames)",
       "Lower is less latency; raise it if audio crackles.", Apply::kRestart, Kind::kInt, {}, 4,
       64},
  }});

  groups.push_back({"SYSTEM", "CPU AND SCHEDULING", "Applies immediately.", {
      {"port_process_priority", "Process priority",
       "Above normal keeps background programs from delaying the game's threads.",
       Apply::kLive, Kind::kChoice,
       {{"normal", "Normal"}, {"above_normal", "Above normal"}, {"high", "High"}}},
      {"port_timer_resolution", "High-precision timer",
       "1 ms Windows timer so the game's 60 Hz tick is delivered on time. Costs a little "
       "battery on laptops.",
       Apply::kLive, Kind::kBool},
      {"port_power_throttling", "Allow Windows power throttling",
       "Off stops Windows moving the game to efficiency cores or slowing it in the "
       "background.",
       Apply::kLive, Kind::kBool},
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
      if (setting.apply == apply) names.push_back(setting.cvar);
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
