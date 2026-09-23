# PC build (developers)

Players don't need any of this; the launcher does it (see [Getting started](GETTING_STARTED.md)). This page is for working on the port from a checkout.

## Setup

| Component | Version or location |
| --- | --- |
| ReXGlue SDK | Pinned Git submodule at `references/rexglue-sdk/` (`0.10.0-dev`), with this port's changes in `patches/rexglue-sdk/sdk.patch` |
| Compiler | LLVM/Clang 22.1.8, CMake 4.x, Ninja |
| Microsoft | Visual Studio 2022 or Build Tools with MSVC v143 and a Windows 11 SDK (headers and libraries only; clang does the compiling) |
| Game | A retail dump staged at `user-game-files/work/mkvsdcu/` (`scripts/stage-game-data.ps1 "<dump>"`) |

The supported `default.xex` is title `4D5707E9`, media `6153914C`, version `0.0.0.1`, SHA-256 `2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7`. The function list in `config/mkvsdcu_functions.toml` belongs to that file only.

## Build and run

```powershell
git submodule update --init --recursive
.\scripts\build-sdk.ps1 -Parallel 12
.\scripts\build-pc.ps1 -Parallel 12
.\scripts\run-pc.ps1
```

`build-sdk.ps1` applies the tracked SDK patch, builds the pinned SDK with D3D12, Vulkan, FidelityFX, profiling and Tracy, and installs it to `references/rexglue-sdk/out/install/win-amd64`. It rejects SDK source edits that are absent from `sdk.patch`, so a release cannot silently contain an uncommitted renderer change. The release script invokes it before packaging the install. The updater distributes the resulting SDK binaries in the versioned port zip; players do not clone the submodule.

To change SDK code, edit `references/rexglue-sdk/`, then export the whole source diff to `patches/rexglue-sdk/sdk.patch` and commit that patch in this repository. For a new SDK source file, use `git -C references/rexglue-sdk add -N <path>` so it appears in `git diff`. On Windows, this command writes the patch without PowerShell changing its encoding:

```powershell
python -c "import pathlib,subprocess; pathlib.Path('patches/rexglue-sdk/sdk.patch').write_bytes(subprocess.check_output(['git','-C','references/rexglue-sdk','diff','--binary','HEAD','--']))"
```

Keep the SDK gitlink at the pinned revision while working with patches. To move to a newer SDK, update the submodule revision, regenerate the patch against that revision, and commit both the gitlink and patch. Test the installed SDK and game before publishing a new release.
Git may show `m` beside the SDK submodule after the patch is applied; that is expected because the pinned upstream checkout contains the port patch. Commit the parent repository's `.gitmodules`, SDK gitlink, patch and scripts. Then run `scripts/release.ps1 -Version <version> -Publish` to publish an update; the launcher checks versioned GitHub releases, so a commit by itself does not trigger a player update.

`build-pc.ps1` checks the XEX hash, writes a local manifest for the game folder, runs ReXGlue codegen when its inputs changed, configures the release preset and builds `out/build/win-amd64-release/mkvsdcu.exe`. `run-pc.ps1` starts it windowed with separate user and cache folders and a new log.

The launcher builds the same way in C# (`launcher/MKVDCU.Launcher/Core/BuildPipeline.cs`), using its own toolchain folder and a PATH limited to it and Windows. Keep the two in step when changing the build.

Useful launch flags (all settable in `mkvsdcu.toml` too):

| Flag | Does |
| --- | --- |
| `--port_perf_csv` | Log one line of performance data per second from launch |
| `--port_profile_after=<s>` | Profile the busiest threads after `s` seconds, writing `profile-*.txt` |
| `--port_mods="<dir>\|<dir>"` | Mod folders to overlay, highest priority first |
| `--port_debug_menu_cycle=<s>` | Open the F1 menu at startup and switch tabs every `s` seconds (for screenshots) |
| `--no-vsync` | Unlock game timing (the game runs faster than normal) |

`scripts/bench-pc.ps1 -Label x -Seconds 150 -GameArgs '--no-vsync'` runs a measured, unattended session. See [Performance](PERFORMANCE.md).

## Crashes from missing functions

`Call to invalid or unregistered function at guest address 0x...` means the recompiler missed a function that is reached only through a pointer. `scripts/advance-functions.ps1` adds that one observed address, regenerates, rebuilds and relaunches. `scripts/find-function-seeds.py` finds such functions in bulk from a dump of the loaded image (`MKVDCU_DUMP_IMAGE=<file>`). Review every address before committing it.

## Codegen options

`config/mkvsdcu_codegen.toml` keeps CTR, XER, CR and reserved registers in C++ locals, which lets the compiler hold them in registers. `non_argument_as_local` crashes this game at startup and stays off. See the file for details.

## Notes

- A full codegen pass reports function `0x82F0F908` over the 1 MiB per-file threshold. It is harmless.
- The tracked SDK patch uses libmspack's canonical source path because cabextract symlinks can become text files on Windows. `build-sdk.ps1` converts FidelityFX's fetched resource from UTF-16LE to UTF-8 for `llvm-rc`.
- The host `CMakeLists.txt` restores `-O3 -DNDEBUG` if a CMake cache ever loses its release flags. An unoptimised build runs the intro movies at 5 FPS.
