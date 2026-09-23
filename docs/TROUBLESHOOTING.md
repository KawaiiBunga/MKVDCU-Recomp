# Troubleshooting

Logs: launcher **Settings → Open logs**, or `%LOCALAPPDATA%\MKVDCU-Recomp\user\logs`. Each game start writes a `play-*.log`. Attach the newest one when you [report a problem](https://github.com/KawaiiBunga/MKVDCU-Recomp/issues/new).

## Setup

**"Unsupported game version"**
Only the original retail release works. If you dumped an installed copy with a title update applied, dump the disc instead. Demos, prototypes and other regions need their own port work.

**"Game folder is incomplete" or "default.xex not found"**
Choose the folder that has `default.xex` directly inside it, not its parent.

**Windows says "Windows protected your PC"**
The launcher isn't code-signed yet. Click **More info → Run anyway**. See [Releasing](RELEASING.md#code-signing) for the plan.

**My antivirus flags or deletes the launcher**
The launcher downloads build tools and starts compilers, which some antivirus heuristics dislike. Every download is checked against a SHA-256 published in the release, and Microsoft's installer is checked for Microsoft's signature before it runs. Compare the file's SHA-256 with `release.json` on the release page, then allow it in your antivirus or report it as a false positive ([Microsoft](https://www.microsoft.com/wdsi/filesubmission)).

**Microsoft's installer failed or was cancelled**
Press **Install build tools** again. It can also be installed by hand: install [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/visual-cpp-build-tools/) with **MSVC v143 x64/x86 build tools** and a **Windows 11 SDK**, then reopen the launcher.

**The build failed**
Open **Show build log**, scroll to the first `error`, and include the log in a report. Common causes:
- Not enough disk space: the build needs about 3 GB free on the install drive.
- Antivirus locking files mid-build: exclude `%LOCALAPPDATA%\MKVDCU-Recomp` and try **Settings → Rebuild game**.
- Damaged compiler tools: **Settings → Reinstall build tools**.

## Playing

**The game closes on startup or during play**
Check the newest `play-*.log`. A line with `Call to invalid or unregistered function` points to a gap in the port; please report it with the log.

**Low frame rate or stutter**
Press F2 for the overlay, then F1 → Performance. The line under the FPS says what is limiting it. Lowering **Render scale** helps a GPU-limited system. The first time new effects appear the game can hitch briefly while shaders compile; this happens once. Vulkan (F1 → Graphics) stutters until its cache fills; Direct3D 12 is recommended.

**No sound, or crackling**
F1 → Audio: check **Mute** and **Master**. For crackling, raise **Buffer** and restart the game.

**Controller not working**
Connect it before starting the game and press a button to wake it. F1 → Controls shows what the game sees.

**Reset the game's settings**
Delete `mkvsdcu.toml` in `%LOCALAPPDATA%\MKVDCU-Recomp\user`. The game recreates it with defaults.

**A mod breaks the game**
Turn it off on the Mods page. The game log lists every mod it loaded (`Mod loaded: …`).
