# Vulkan fixes and builds (2026-09-30)

The complete PC SDK patch is `sdk.patch`, based on the pinned `references/rexglue-sdk` submodule. It preserves the existing port changes. `vulkan-common.patch` contains the shared texture exponent, guest swap pacing, and SPIR-V dump changes for the separate Switch SDK under `references/nfsmw-nx-main/sdk` (an ignored vendor tree). The Switch build script checks/applies this patch without overwriting Switch-specific code.

* Green startup videos / black screen: SPIR-V texture fetch exponent adjustment must read fetch constant DWORD 3, not DWORD 4 (which contains LOD bias). Incorrect exponent scaling destroyed sampled texture colors.
* Missing hangar floors: MKvDCU uses Vulkan fragment shader interlock (`fsi`) with legacy render passes (`vulkan_dynamic_rendering=false`) by default on both platforms. The PC hangar floor was confirmed visible using this configuration. The host FBO path still has a rendering defect and remains an explicit performance option; interlock can be slower. Switch NVK reports fragment shader interlock support, but the rebuilt NRO still needs an on-device rendering check.
* Normal speed: `gpu_frame_rate_limit=60` paces guest GPU swaps independently of presentation. `vsync=false` (the port's Unlocked mode) bypasses this cap. A zero limit disables the new limiter. Delayed frames reset the schedule rather than triggering a catch-up burst.

Existing user config overrides are preserved. For the verified rendering path, use:

```toml
render_target_path_vulkan = "fsi"
vulkan_dynamic_rendering = false
gpu_frame_rate_limit = 60
vsync = true
```

PC: run `scripts/build-sdk.ps1` then `scripts/build-pc.ps1`. This machine's old SDK CMake cache referred to a moved checkout and a different Clang toolchain. The fixed SDK was built with a fresh Ninja Release directory, `references/rexglue-sdk/out/build/win-amd64-vulkan-fixed`, Clang 22.1.8, Vulkan + D3D12 + FidelityFX + Tracy + perf counters, and installed to the normal `references/rexglue-sdk/out/install/win-amd64`. Build logs and exact configure arguments are under `research`. The stale PC game build was preserved as `win-amd64-release-before-vulkan-fix` before configuring the normal output directory in this repository.

Switch: run `scripts/build-switch.ps1 -Parallel 3`; output is `targets/mkvsdcu-nx/build-switch/mkvsdcu.nro` and `dist/mkvsdcu.nro`. Toolchain: `references/nfsmw-nx-main/tools/switch/cmake/switch-devkitA64.cmake`; Mesa portlibs: `mesa-sdk/opt/devkitpro/portlibs/switch`. Start through full-memory homebrew/application takeover.

Temporary frame capture and depth comparison experiments are excluded from both release builds. No unrelated source edits or repository revisions were reset.

Validation: the complete PC SDK patch passed source consistency/reverse-apply checks and built/installed successfully through `scripts/build-sdk.ps1`. The PC game built successfully through `scripts/build-pc.ps1`. On the rebuilt PC Vulkan version, a 30 FPS diagnostic cap measured 30 FPS (33.3 ms) and normal 60 measured 60 FPS (16.7 ms) in steady startup-video samples. Raw guest VdSwap telemetry is in `research/vulkan-final-verify/cap30.csv` and `cap60.csv`; loading transitions can reduce the frame rate. The earlier visible hangar-floor comparison is copied there as `fsi-floor.png`.

Final Switch NRO: 89,106,629 bytes; SHA-256 `E79A1C1E4A88607DE552DFC113BB4DD9F310EF5BA68F1D5112E0DBC66E23273A`. Packaging completed successfully and NRO/asset headers validated. `dist/mkvsdcu.toml` contains the settings above as flat cvar names; `[graphics]` would incorrectly prefix them with `graphics_`. Copy it beside the NRO after preserving any existing config settings you want to retain. The original FTP config was read for diagnosis; deployment and on-device verification remain for the next run.
