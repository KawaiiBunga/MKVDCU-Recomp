# Getting started

MKVDCU-Recomp currently targets **Windows PC**. The local build uses the retail Xbox 360 base executable identified in [Status](STATUS.md). Switch packaging research is retained separately and does not run the game.

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

The Windows host booted and completed an Arcade match with an Xbox controller and sound on 22 September 2026. Other modes, long sessions, other PCs, and Switch gameplay have not been validated. The next planned feature is an in-game port menu; see [Roadmap](ROADMAP.md).
