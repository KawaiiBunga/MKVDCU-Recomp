# Status — 22 September 2026

## PC: playable path verified

- The retail Xbox 360 base build of Mortal Kombat vs. DC Universe (`4D5707E9`, media ID `6153914C`, XEX version `0.0.0.1`) is recompiled with ReXGlue `0.10.0-dev.gc94f5eb` and runs as a Windows AMD64 application.
- The host uses the official Xenos GPU plugin with Direct3D 12. Graphics pipelines and shader storage initialize, the Xbox One controller is detected, and audio plays through the PC endpoint.
- On 22 September, the user completed an entire Arcade mode match with sound and no crash. The run then closed cleanly when the window was closed. The local log records `Window closing, shutting down` and `Execution complete`, with no fatal entry.
- Several earlier starts and gameplay runs exposed missing runtime-computed function targets. Those exact addresses were added to the tracked exact-XEX function config, followed by codegen and rebuild. The current executable includes those verified entries.

This is one tested gameplay path on one PC. Other modes, characters, arenas, longer sessions, save behavior, different controllers and GPUs, and performance targets remain to be tested. A successful launch or a process staying alive alone does not establish playability.

## Windows launcher and port menu

- The WPF launcher validates the exact supported XEX SHA-256 and required game content folders before building or launching. It stores separate install, user, and cache paths and copies the three native host binaries into the install root after a successful local build.
- The release packaging script produces one self-contained `MKVDCU-Recomp.exe`. The EXE embeds the port source, updater helper, and pinned ReXGlue SDK. Its GitHub Releases ZIP contains the same EXE and a compatibility manifest for verified updates.
- The build script now uses .NET SHA-256 hashing compatible with Windows PowerShell 5.1. A Windows PowerShell invocation completed codegen, CMake, and native build with the staged retail XEX on this machine. The script also avoids the duplicate full XEX scan in CMake; a verified no-change build now reports `ninja: no work to do`.
- The F1 port menu has Display, Graphics, Performance, Controls, Audio, System and About pages, each split into settings that apply live and settings saved for the next launch. Settings are written by a checked TOML writer (the SDK serializer left Windows paths unescaped, so saved settings never loaded); broken configs are repaired at startup. Controller deadzones, remapping, rumble strength, stick-to-D-pad and a Back+Start menu chord go through a host input filter; they are built but not yet tested with a physical controller.
- F2 toggles a performance overlay fed by a `VdSwap` hook (true game frame rate, frame-time percentiles, hitches, game output resolution) plus per-thread CPU, GPU engine load, VRAM and RAM, with a bottleneck readout and optional per-second CSV log.
- Frame rate, measured 2026-09-23 in an Arcade match: the simulation advances once per presented frame. With the guest vblank unlocked the game rendered 72–73 FPS and the round clock ran ~27% fast. There is no safe FPS unlock without engine-level changes, and presenter-side frame interpolation has no motion or depth data to work from. At 1x/720p on a GTX 1650 Super the game holds 60 FPS with the GPU ~90% busy and two guest threads near a full core each.
- Rendering options, 23 September: the SDK was rebuilt with Vulkan and AMD FidelityFX. The menu now picks the graphics API (Direct3D 12 or Vulkan), GPU adapter, output filter (bilinear, CAS, FSR 1, FSR 2/3 quality modes and sharpening), render target path, accuracy and readback toggles, and shader/texture cache limits. It also sets audio volume and surround downmix, plus CPU scheduling (priority, timer resolution, power throttling). Direct3D 11 and OpenGL/GLES have no backend in the SDK; see [Performance](PERFORMANCE.md).
- Stability, 23 September: the `0x826AF018` "unregistered function" crash, and 153 more targets reachable only through vtables or callbacks, were found by `scripts/find-function-seeds.py` and added to the function config. A build with them ran 13 minutes idle; the previous build crashed after ~5.
- The launcher and port menu are new. A full end-to-end build, update, F1 interaction, and match using the packaged EXE have not yet been verified on a fresh Windows PC.

The packaged EXE does not contain the user's game, LLVM/Clang, CMake/Ninja, or Microsoft C++ Build Tools and Windows SDK. Those local build prerequisites remain the main one-click installation gap. The launcher UI now uses a custom title bar and release-channel buttons, setup browse controls, direct status copy, and original metal/versus styling.

## Switch: toolchain only

`switch/toolchain-test/` builds a small libnx NRO. There is no linked ReXGlue game runtime, Xenos rendering path, or playable Switch build yet. The host runtime and menu/configuration layer should be stabilized before that port.

## Known work

1. Continue PC gameplay coverage and fix concrete crashes from logs. Keep each function address tied to the exact staged XEX.
2. Test and complete the in-game port menu, including timing, audio, and controller mapping. Keep options capability-aware so Switch can use the same design later.
3. Profile and validate frame pacing, resolution changes, rendering enhancements, controller mapping, and save/config behavior.
4. Port the runtime, graphics, audio, filesystem, and input layers to Switch; package and test the actual game on device.

See [Roadmap](ROADMAP.md) for the next feature phase and [PC build](PC_BUILD.md) for the local workflow.
