# Getting started

MKVDCU-Recomp currently targets **Windows PC**. The local build uses the retail Xbox 360 base executable identified in [Status](STATUS.md). Switch packaging research is retained separately and does not run the game.

## Launcher path

Run the packaged `MKVDCU-Recomp.exe` or build one with `scripts/package-launcher.ps1`. It is a self-contained Windows launcher that includes its updater, the port build files, and the pinned ReXGlue SDK. Select your extracted game folder, verify it, then choose **Build Game** and **Play**. The game folder stays where it is. Press **F1** during play for the first port menu.

The selected directory must contain `default.xex` plus `Asset/`, `Config/`, `Localization/`, and `Movies/`. The launcher accepts only the retail base executable with SHA-256 `2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7`. It will show an unsupported-revision message for another XEX.

Local compilation still requires LLVM/Clang, CMake, Ninja, and Microsoft C++ Build Tools with the Windows SDK on the PC. The first build can take several minutes. The launcher keeps the native game build under the selected install root, saves and config under the user root, and shader cache under the cache root. These can be changed on **Setup** with Browse buttons. Leave the optional ReXGlue SDK override empty to use the copy inside the EXE. After a successful build, normal play works offline.

## Existing prepared workspace

From the repository root in PowerShell:

```powershell
.\scripts\doctor.ps1 -Mode pc
.\scripts\build-pc.ps1
.\scripts\run-pc.ps1
```

`run-pc.ps1` starts a windowed offline game with the Xenos GPU plugin, controller support, audio enabled, and a separate log. The script prints the log path and process ID. The game files, generated code, build output, and runtime data are all local and ignored by Git.

## Preparing your own dump

Supply a **legally obtained, complete, extracted Xbox 360 retail game directory** containing `default.xex`, `Asset/`, `Movies/`, configuration, and localization files. Keep the original outside this repository and unchanged. This project is tied to one exact XEX revision; a different region, title update, or prototype requires separate analysis and function addresses.

```powershell
.\scripts\stage-game-data.ps1 "D:\My MK vs DC Universe Dump"
```

This validates the root-level `XEX2` signature, copies files to an **empty** ignored `user-game-files/work/mkvsdcu/` directory, and writes SHA-256 hashes to an ignored report. It does not apply title updates or create a working recompilation by itself. A fresh clone also needs the ReXGlue SDK, a matching XEX, codegen, and a host build; see [PC build details](PC_BUILD.md).

## What is currently verified

The Windows host booted and completed an Arcade match with an Xbox controller and sound on 22 September 2026. Other modes, long sessions, other PCs, and Switch gameplay have not been validated. The launcher and first F1 menu implementation are being validated now; see [Status](STATUS.md) and [Roadmap](ROADMAP.md).
