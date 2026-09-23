# MKVDCU-Recomp

An Xbox 360 **Mortal Kombat vs. DC Universe** recompilation built with ReXGlue. **PC is the active target.** Nintendo Switch is a later port target; the current Switch code only proves that the libnx toolchain can build a small homebrew application.

On 22 September 2026, the Windows host build booted the retail base game, played an entire Arcade match with an Xbox controller and sound, and closed cleanly. That is a verified playable path, not a claim that every mode or hardware configuration works.

## Launcher (Windows)

The Windows launcher uses original charcoal, metal, red, and blue visuals inspired by the game's 2008 menus. It checks the exact retail base XEX, builds the host locally, and starts the game. Its **F1** in-game port menu currently exposes display, graphics, keyboard mapping, and system settings. More timing and controller options need gameplay validation before they can be enabled.

The packaged single EXE (`dist/MKVDCU-Recomp.exe`) contains the launcher, its updater helper, port build source, and the pinned ReXGlue SDK. Choose your extracted game folder (the directory with `default.xex`), press **Verify**, then **Build Game**, then **Play**. The executable and generated game code are built on your PC; the package does not include game files. LLVM/Clang, CMake, Ninja, and Microsoft C++ Build Tools with the Windows SDK are currently required for the first build. The launcher shows build errors in its Build Log. Run `scripts/package-launcher.ps1` to create a new single EXE and GitHub Releases update ZIP.

## Run from the source checkout

The playable local build uses an ignored copy of a legally obtained Xbox 360 game dump and ignored generated PPC code. The internal target ID is `mkvsdcu`; MKVDCU-Recomp is the project name. In this workspace:

```powershell
.\scripts\doctor.ps1 -Mode pc
.\scripts\build-pc.ps1
.\scripts\run-pc.ps1
```

For setup from your own extracted game directory, see [Getting started](docs/GETTING_STARTED.md) and [PC build details](docs/PC_BUILD.md). The repo does not contain the game executable, assets, generated PPC output, or shader cache. The release build embeds an SDK binary install; the source repo does not track that install.

## Project map

| Path | Purpose |
| --- | --- |
| `targets/mkvsdcu/private/rexglue-host/` | ReXGlue PC app source, CMake project, and exact-XEX function addresses. Generated code, builds, caches, and logs in this directory remain ignored. |
| `scripts/` | Stage a legal dump, build and run the PC host, and advance verified indirect function targets. |
| `launcher/` | Windows WPF launcher and out-of-process update helper. |
| `docs/STATUS.md` | What has actually been tested and what remains open. |
| `docs/ROADMAP.md` | PC stabilization and the planned in-game port menu. |
| `switch/toolchain-test/` | Small libnx/NRO toolchain test; it is not the game. |
| `docs/switch/` and `research/` | Switch port research and earlier target-selection work. |

The launcher and first port menu implementation are in progress. Switch-specific options will be added when a working Switch runtime exists.

The [launcher and port menu plan](docs/LAUNCHER_AND_PORT_MENU_PLAN.md) covers game-folder validation, local building, F1 overlay, settings, and GitHub Releases updates.
