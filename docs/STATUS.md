# Status — 22 September 2026

## PC: playable path verified

- The retail Xbox 360 base build of Mortal Kombat vs. DC Universe (`4D5707E9`, media ID `6153914C`, XEX version `0.0.0.1`) is recompiled with ReXGlue `0.10.0-dev.gc94f5eb` and runs as a Windows AMD64 application.
- The host uses the official Xenos GPU plugin with Direct3D 12. Graphics pipelines and shader storage initialize, the Xbox One controller is detected, and audio plays through the PC endpoint.
- On 22 September, the user completed an entire Arcade mode match with sound and no crash. The run then closed cleanly when the window was closed. The local log records `Window closing, shutting down` and `Execution complete`, with no fatal entry.
- Several earlier starts and gameplay runs exposed missing runtime-computed function targets. Those exact addresses were added to the tracked exact-XEX function config, followed by codegen and rebuild. The current executable includes those verified entries.

This is one tested gameplay path on one PC. Other modes, characters, arenas, longer sessions, save behavior, different controllers and GPUs, and performance targets remain to be tested. A successful launch or a process staying alive alone does not establish playability.

## Switch: toolchain only

`switch/toolchain-test/` builds a small libnx NRO. There is no linked ReXGlue game runtime, Xenos rendering path, or playable Switch build yet. The host runtime and menu/configuration layer should be stabilized before that port.

## Known work

1. Continue PC gameplay coverage and fix concrete crashes from logs. Keep each function address tied to the exact staged XEX.
2. Add a clean in-game port menu and persistent configuration for PC graphics, timing, audio, and controls. Keep options capability-aware so Switch can use the same design later.
3. Profile and validate frame pacing, resolution changes, rendering enhancements, controller mapping, and save/config behavior.
4. Port the runtime, graphics, audio, filesystem, and input layers to Switch; package and test the actual game on device.

See [Roadmap](ROADMAP.md) for the next feature phase and [PC build](PC_BUILD.md) for the local workflow.
