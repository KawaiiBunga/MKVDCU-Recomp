# Switch performance and port menu

The Switch target lives in `targets/mkvsdcu-nx`. Build with native Windows PowerShell:

```powershell
./scripts/build-switch.ps1 -Parallel 3
```

The output is `dist/mkvsdcu.nro`, `dist/mkvsdcu.toml`, and a SHA-256/ELF/package verification report. Keep the unstripped `targets/mkvsdcu-nx/build-switch/mkvsdcu` for decoding crashes. Use full-memory application takeover, not album applet mode.

## Controller menu

Hold **L + R**, then press **D-pad Right**. D-pad navigates the menu. A selects and B goes back; with Xbox face-button positions enabled, B selects and A goes back. Touch controls also work. Controller input is consumed by the menu while it has focus. The simulation continues running underneath it.

The menu offers renderer selection, frame-rate ceilings, ring-wait interval, audio/input controls, diagnostics, settings saving and the SDK's advanced settings. Renderer changes require saving and restarting. Settings are written beside the NRO; SD filesystems that cannot rename over an existing file use a saved `.before-port-menu` backup before overwriting.

Hold **L + R + Up** for the performance overlay, **Down** for the console, or **Left** for achievements. FPS counts guest frame handoffs, not UI redraws or display vblanks.

## Changes based on NFSMW's documentation

Reviewed [NFSMW's docs](https://github.com/StevensND/nfsmw-nx/tree/main/docs), including toolchain, measurement, native rendering, shaders, audio/video, Mesa, platform constraints and console overlays.

| Technique | MKvDCU status |
| --- | --- |
| Direct calls plus GCC LTO | Already 166,515 direct guest calls. Build staging preserves weak calls to protected addresses and handles fresh code generation without altering its input files or indirect dispatch tables. |
| Hook correctness and NRO startup checks | Packaging rejects a missing strong ring-wait hook or frame-counter wrapper, a nonzero `_start`, or invalid NRO/ASET headers. |
| xxHash strict aliasing | Switch xxHash compiled with `XXH_FORCE_MEMORY_ACCESS=0` for memcpy-safe loads. |
| Busy waits | Default ring sleep increased from 20 to 200 microseconds. Optional diagnostics use thread-local counters; no additional title diagnostic clock reads or statistics atomics in the ordinary wait hook. The SDK retains its passive system-call counters. Original guest state transitions still run. |
| Compilation worker budget | Two Vulkan pipeline workers leave CPU time for the game and audio during warmup. |
| Official handheld GPU modes | Optional 384/460.8 MHz request through the existing SDK APM helper, with unchanged memory clock required. Default 0 preserves existing sys-clk/overclock profiles. Requires restart. |
| Diagnostics overhead | Routine watchdog snapshots and ring-wait statistics off by default. Stall detection remains enabled. |
| Accurate FPS and port settings | Controller-driven title menu, persistent flat TOML settings, guest-frame telemetry. |
| Async XMA, lazy achievements, guest mapping, exceptions, cooperative priorities, SaltyNX/ReverseNX | Already present in the vendored Switch SDK. Retained. |
| MAX with identical operands as a copy | Already implemented in this SDK's SPIR-V translator. Retained. |
| Shader disk cache | Retained: this emulated renderer has no persistent `VkPipelineCache`, so disabling Mesa's cache would remove useful caching. NFSMW's native renderer has its own driver cache. |
| Nonvolatile register locals / PGO / function order | Do not apply NFSMW's address lists or profiles. MKvDCU previously broke with the register options; it needs its own split-function/register-sharing audit and measured profile. Existing safe local-register settings retained. |
| Native renderer, native audio/video hooks, offline native shaders | NFSMW-specific game addresses, layouts and shaders cannot be copied into MKvDCU. These require MKvDCU-specific reverse engineering and correctness comparisons. |
| New Mesa compiler/ZCULL changes | Local driver is the older Mesa SDK and predates the newer NFSMW patch. A full driver rebuild needs a Mesa source checkout and MSYS2 host compiler/LLVM/bindgen tools absent here; no claim that those changes are in this NRO. |

## Rendering and measurement limits

Accurate rendering uses `render_target_path_vulkan = "fsi"` and `vulkan_dynamic_rendering = false`. This restored the hangar floor in the PC comparison. FSI emulates Xenos depth/stencil and blending through fragment shader interlock and is expensive on the Switch. The experimental FBO option is exposed for comparison but can omit floors; it is not a correctness fix. Native 2x MSAA stays disabled for the NVK stability workaround.

The FTP baseline logs show about 2.4-2.9 FPS in slow sections, with GPU Commands using roughly 8-10% of a CPU core. This suggests the rendering path is the main bottleneck; CPU changes alone do not establish a 30/60 FPS solution. Those logs also contain elevated console clocks and a suspend interval: do not present them as stock-clock results or include suspend time in averages.

Compare the same paused fight scene and the same console clocks. Warm shader caches first. Change one option at a time; the ring-wait interval can be changed live for comparison. A frame-rate ceiling limits fast frames and cannot make a slow scene hit its target. Keep `vsync=true` with a 60 FPS ceiling for normal speed; unlocked mode bypasses guest pacing and can change game speed.

The old deployed config used `[graphics]`; this SDK prefixes its keys as `graphics_*`, leaving them unknown. The packaged TOML uses top-level cvar names. Preserve a copy of the old configuration before replacing it.
