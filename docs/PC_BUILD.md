# PC build and runtime

## Verified local setup

| Component | Local version or location |
| --- | --- |
| ReXGlue SDK | `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` (`0.10.0-dev.gc94f5eb`) in ignored `references/rexglue-sdk/` |
| Host tools | Windows AMD64, Clang 22.1.8, CMake 4.4.3, Ninja |
| Game input | Ignored `user-game-files/work/mkvsdcu/`, copied from one legal retail base dump |
| PC project | `targets/mkvsdcu/private/rexglue-host/` |
| GPU | Official `rexgpu-xenos` plugin using Direct3D 12; tested on NVIDIA GTX 1650 SUPER |
| Audio and input | Stereo endpoint and Xbox One controller through ReXGlue/SDL |

The exact XEX, content inventory, and hashes are in the local ignored `targets/mkvsdcu/reports/bring-up.json` and `host-bringup.md`. The staged executable is title ID `4D5707E9`, media ID `6153914C`, version `0.0.0.1`, with no title update or companion XEX/DLL. Generated PPC output stays ignored. The verified indirect function addresses are tracked in `config/mkvsdcu_functions.toml` and apply only to the XEX whose SHA-256 is recorded there.

## Build and launch here

```powershell
.\scripts\doctor.ps1 -Mode pc
.\scripts\build-pc.ps1
.\scripts\run-pc.ps1
```

The build script verifies the exact supported XEX SHA-256, writes an ignored local manifest for the selected game folder, runs ReXGlue codegen when its inputs change, configures the Windows Release preset, and builds `mkvsdcu.exe`. CMake's build step updates the codegen stamp without repeating the same full XEX scan. It accepts `-GameDataRoot` and `-SdkRoot`, which the launcher passes after validation. Direct CMake builds can restore SDK-managed codegen with `-DMKVSDCU_CODEGEN_MANAGED_EXTERNALLY=OFF`. The launch script uses `--gpu_plugin xenos` and `--no-audio_mute`, points at the ignored game, user, and cache directories, and saves a uniquely named log. It does not enable online services.

The packaged launcher is built with `scripts/package-launcher.ps1`. It embeds the source files, updater helper, and the installed pinned ReXGlue SDK into one `dist/MKVDCU-Recomp.exe`. A user still needs a matching extracted game and local LLVM/Clang, CMake, Ninja, and Microsoft C++ Build Tools with the Windows SDK for the first build. The launcher stores compiled game binaries, saves, settings, logs, and shader caches outside its own EXE. The release ZIP is for the updater; the EXE can be launched directly. Once built, normal play does not run the compiler or require network access.

The EXE is a self-contained **launcher**, not yet a zero-prerequisite game builder. LLVM, CMake, and Ninja can be packaged as versioned portable tools after testing their redistribution terms, dependency files, and size. The current native SDK and CMake build also use Microsoft's C++ headers and Windows import libraries. Do not copy a local Visual Studio or Windows SDK installation into the release without establishing redistribution rights for each component. A one-click first build can instead arrange the official Build Tools/Windows SDK installer and report any elevation or download requirement. A fully offline build from only this EXE and the retail game would need a validated toolchain that can be redistributed with the package, including compatible C++ and Windows headers/libraries.

On the verified SDK revision, a full codegen pass reports that function `0x82F0F908` exceeds its 1 MiB per-file threshold. The pass completes, leaves all 443 generated C++ files unchanged, and the PC build succeeds. Keep this diagnostic in mind if codegen or compilation changes on a newer SDK.

When a run fails with `Call to invalid or unregistered function at guest address`, `scripts/advance-functions.ps1` can record **only that observed address** in the function config, regenerate, rebuild, and relaunch. Review each proposed address before committing the config. Review the log if the process exits for any other reason. A clean window close is a normal end to a play test.

## Fresh clone limitations

The repo tracks the host app source, CMake setup, manifest, exact-XEX function addresses, and build/launch scripts. It does **not** ship the Xbox executable, assets, generated C++, binaries, runtime cache, or the SDK checkout. A new checkout needs a legally obtained dump matching the recorded SHA-256 and an installed ReXGlue SDK to reproduce the current build. Another XEX revision needs fresh analysis; do not reuse these addresses or invent an update requirement.

The local SDK checkout needed a Windows-only `libmspack` source-selection fix because cabextract symlinks materialized as text files. That change is confined to the ignored SDK checkout; no game or ReXGlue runtime logic was patched to reach the verified Arcade match.
