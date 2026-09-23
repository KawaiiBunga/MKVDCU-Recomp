# MKVDCU-Recomp

An unofficial PC port of the Xbox 360 game **Mortal Kombat vs. DC Universe**, made by statically recompiling the game with [ReXGlue](https://github.com/rexglue/rexglue-sdk). You supply your own copy of the game; the launcher builds the PC version on your machine.

## Play

1. Download `MKVDCU-Recomp.exe` from [Releases](https://github.com/KawaiiBunga/MKVDCU-Recomp/releases/latest) and run it.
2. **Choose game folder**: the folder you copied the game to, with `default.xex` in it.
3. **Install build tools**: a one-time download. Windows asks once for permission to run Microsoft's installer.
4. **Build game**: 5 to 20 minutes the first time, much faster after updates.
5. **Play.** In game, **F1** (or hold **Back + Start**) opens the settings menu and **F2** shows the performance overlay.

[Getting started](docs/GETTING_STARTED.md) covers each step in detail. If something goes wrong, see [Troubleshooting](docs/TROUBLESHOOTING.md). The launcher updates itself and the port, and has a **Mods** page ([making mods](docs/MODDING.md)).

Only the original retail release is supported (SHA-256 of `default.xex`: `2955F2E2…48E5A7`). The project does not include or distribute any game files.

## Status

Tested on one PC: the game boots, plays full Arcade matches with a controller and sound, and holds 60 FPS on a GTX 1650 SUPER. Other modes, hardware and long sessions need more testing. See [Status](docs/STATUS.md) and [Performance](docs/PERFORMANCE.md).

## Develop

| Path | What it is |
| --- | --- |
| `targets/mkvsdcu/private/rexglue-host/` | The PC host: CMake project, F1 menu, telemetry, mod loader, and the function list for the supported XEX |
| `launcher/MKVDCU.Launcher/` | The Windows launcher (WPF, .NET 8) |
| `scripts/` | Build and run from a checkout, benchmark, make releases |
| `mods/index.json` | The mod list the launcher offers |
| `docs/` | Player guides, build notes, releasing |

Build and run from a checkout with the steps in [PC build](docs/PC_BUILD.md). Release process: [Releasing](docs/RELEASING.md). Switch is a later target; `switch/toolchain-test/` only proves the libnx toolchain.
