# MKVDCU-Recomp

An Xbox 360 **Mortal Kombat vs. DC Universe** recompilation built with ReXGlue. **PC is the active target.** Nintendo Switch is a later port target; the current Switch code only proves that the libnx toolchain can build a small homebrew application.

On 22 September 2026, the Windows host build booted the retail base game, played an entire Arcade match with an Xbox controller and sound, and closed cleanly. That is a verified playable path, not a claim that every mode or hardware configuration works.

## Run the current PC build

The playable local build uses an ignored copy of a legally obtained Xbox 360 game dump and ignored generated PPC code. The internal target ID is `mkvsdcu`; MKVDCU-Recomp is the project name. In this workspace:

```powershell
.\scripts\doctor.ps1 -Mode pc
.\scripts\build-pc.ps1
.\scripts\run-pc.ps1
```

For setup from your own extracted game directory, see [Getting started](docs/GETTING_STARTED.md) and [PC build details](docs/PC_BUILD.md). The repo does not contain the game executable, assets, generated PPC output, shader cache, or a redistributable SDK build.

## Project map

| Path | Purpose |
| --- | --- |
| `targets/mkvsdcu/private/rexglue-host/` | ReXGlue PC app source, CMake project, and exact-XEX function addresses. Generated code, builds, caches, and logs in this directory remain ignored. |
| `scripts/` | Stage a legal dump, build and run the PC host, and advance verified indirect function targets. |
| `docs/STATUS.md` | What has actually been tested and what remains open. |
| `docs/ROADMAP.md` | PC stabilization and the planned in-game port menu. |
| `switch/toolchain-test/` | Small libnx/NRO toolchain test; it is not the game. |
| `docs/switch/` and `research/` | Switch port research and earlier target-selection work. |

The next feature phase is an in-game port menu for frame rate, resolution, visual enhancements, and controls. PC behavior and configuration will be made reliable first; Switch-specific options will be added when a working Switch runtime exists.
