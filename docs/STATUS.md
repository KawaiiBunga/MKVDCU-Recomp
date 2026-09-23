# Status — 23 September 2026

## Game

- The retail Xbox 360 release (`default.xex` title `4D5707E9`, media `6153914C`, version `0.0.0.1`) is recompiled with ReXGlue `0.10.0-dev.gc94f5eb` and runs as a 64-bit Windows program with Direct3D 12 (Vulkan optional).
- Verified on one PC (GTX 1650 SUPER): full Arcade matches with an Xbox controller and sound, clean exit. Holds 60 FPS at 1x (720p); unlocked timing reaches ~130 FPS, limited by the GPU. See [Performance](PERFORMANCE.md).
- Stability: missing-function crashes seen so far are fixed, including `0x826AF018` and 153 more functions found by scanning the image for pointer tables. Untested modes may still hit new ones; each report with a log is quick to fix.
- Not yet tested: other modes in depth, long sessions, saves across versions, AMD/Intel GPUs, other controllers.

## In-game menu (F1)

Seven tabs: Display, Graphics, Controls, Audio, Performance, Advanced and About. All settings come from one catalog, save automatically, and show a **restart** tag when they apply at the next launch. The help line at the bottom describes the row under the cursor. Every tab was checked on screen on 23 September with the menu's screenshot mode. The controller features (deadzones, remapping, rumble, stick-to-D-pad, the Back + Start chord) are built but haven't been tested with a physical controller. F2 shows the performance overlay; F1 → Performance has a CSV log and a CPU profiler.

## Mods

File-replacement mods work: the host adds mod files into the game's file tree at startup, and the player's game folder is never written. Verified on 23 September by replacing an intro movie with a different-sized file; the replacement played. See [Making mods](MODDING.md). `mods/index.json` is empty until the first mods are listed.

## Launcher and releases

- One self-contained `MKVDCU-Recomp.exe` (~140 MB). It needs no admin rights, uses no PowerShell, and has no embedded extra executables.
- First run: choose the game folder, then install build tools. That means Microsoft's signed Build Tools installer (only if MSVC is missing), plus a 130 MB LLVM/CMake/Ninja zip from the release. Then build and play.
- A full build with only the downloaded toolchain and Microsoft's libraries (PATH limited to them) compiled and linked all 237 steps in 5.8 minutes.
- Updates: `release.json` per GitHub release, self-replacing EXE, incremental rebuilds. `scripts/release.ps1` builds and publishes a release.
- Not yet done: a published release and an update between two published releases; a run on a PC without Visual Studio (Microsoft's installer path); code signing.

## Switch

`switch/toolchain-test/` builds a small libnx program. There is no Switch game build.

See [Roadmap](ROADMAP.md) for what's next.
