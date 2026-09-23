#pragma once

#include <string>
#include <vector>

// Host-side settings that are not SDK cvars: Windows scheduling, audio mix
// and the graphics backend. Each applies itself when its cvar changes.
namespace host_tweaks {

// Applies every setting once and keeps them applied on change. Call after
// the config has been loaded and the runtime is set up.
void Install();
void Uninstall();

// "d3d12" or "vulkan", for loading the GPU plugin before the runtime starts.
std::string GpuBackend();

// Backends the installed GPU plugin was compiled with.
std::vector<std::string> AvailableGpuBackends();

}  // namespace host_tweaks
