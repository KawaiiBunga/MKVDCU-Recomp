# Getting started

## What you need

- Windows 10 (version 1809 or later) or Windows 11, 64-bit
- A graphics card with Direct3D 12 support
- About 15 GB of free disk space: 6 GB for the game files, about 4 GB for Microsoft's C++ Build Tools, and 3 GB for the build
- An internet connection for the first setup and for updates. Playing needs no connection.
- Your own copy of **Mortal Kombat vs. DC Universe** for Xbox 360, retail release, without title updates

## 1. Get the game files

The launcher needs the game's files in a normal folder: `default.xex` plus the `Asset`, `Config`, `Localization` and `Movies` folders.

- **From your disc:** make an ISO of your disc with a tool that supports Xbox 360 discs, then extract the ISO with [extract-xiso](https://github.com/XboxDev/extract-xiso) (`extract-xiso -x game.iso`).
- **From your console's hard drive:** copy the installed game folder to your PC.

Keep this folder somewhere permanent, for example `D:\Games\MKvsDC`. The launcher never changes it.

## 2. Run the launcher

Download `MKVDCU-Recomp.exe` from the [latest release](https://github.com/KawaiiBunga/MKVDCU-Recomp/releases/latest) and put it anywhere you like, for example a `MKVDCU-Recomp` folder. It is the only file you need; it updates itself.

The launcher isn't code-signed yet, so on first launch Windows SmartScreen may say "Windows protected your PC". Click **More info → Run anyway**. The file's SHA-256 is listed in the release's `release.json` if you want to check it.

## 3. Follow the three steps on Play

The **Setup** card on the right shows what is done. The big button always does the next step.

1. **Choose game folder.** Pick the folder from step 1. The launcher checks it is the supported retail version; a different version or a missing folder shows what's wrong.
2. **Install build tools.** This installs two things once:
   - **Microsoft C++ Build Tools and the Windows SDK** (about 3 GB), with Microsoft's own installer. Windows asks for permission, and Microsoft's progress window appears. Microsoft doesn't allow these to be bundled.
   - **Compiler tools** (LLVM, CMake and Ninja, 130 MB). These are unpacked into the launcher's own folder and don't change your system.
3. **Build game.** Turns your game's Xbox 360 code into a Windows program. The first build takes 5 to 20 minutes, depending on your CPU. **Show build log** shows what it's doing.

Then press **Play**.

## Playing

| Key or button | Does |
| --- | --- |
| F1, or hold Back + Start | Settings: display mode, resolution, upscaling, graphics, controls, audio |
| F2 | Performance overlay |
| LB / RB or Page Up / Page Down | Switch tabs in the settings menu |

Settings save automatically. Settings marked **restart** apply the next time you start the game.

## Updates and mods

When a new version is out, the Play page shows **Update**. The launcher replaces itself, then rebuilds only what changed. Pick **Stable** or **Preview** updates in Settings.

The **Mods** page installs mods from the list or from a `.zip`, turns them on and off, and sets their order. See [Making mods](MODDING.md).

## Where things are

| What | Where |
| --- | --- |
| Launcher settings, compiler tools, mods | `%LOCALAPPDATA%\MKVDCU-Recomp` |
| PC build | `%LOCALAPPDATA%\MKVDCU-Recomp\install` (changeable in Settings) |
| Saves, game settings, logs | `%LOCALAPPDATA%\MKVDCU-Recomp\user` (changeable) |
| Shader cache | `%LOCALAPPDATA%\MKVDCU-Recomp\cache` (changeable) |

To uninstall, delete the launcher and `%LOCALAPPDATA%\MKVDCU-Recomp`. Microsoft's Build Tools can be removed in **Settings → Apps** ("Visual Studio Build Tools 2022").
