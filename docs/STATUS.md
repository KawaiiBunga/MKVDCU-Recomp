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
- The F1 port menu is implemented with display, graphics, performance, controls, and system pages. Fullscreen and VSync can be changed live; render scale and anisotropy are saved for restart; keyboard face-button mappings can be edited. FPS unlock, downscaling, and wider control remapping still need runtime work and validation.
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
