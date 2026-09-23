# Launcher and port menu plan

Status: design plan, 22 September 2026. The PC host currently boots the matching retail base XEX and has completed one Arcade match. The launcher, updater, and custom port menu described here have not been implemented.

## Product contract

**MKVDCU-Recomp Launcher** takes a user's extracted Xbox 360 game folder, verifies that it is the supported build, creates a local PC build, and starts it. **The Port Menu** is an overlay inside the running game, opened with F1 or a configurable controller chord. Both use the same versioned settings model. Windows PC is the first shipping target; a future Switch port can reuse setting definitions without carrying the Windows launcher over.

The first public experience should be: install launcher → choose game folder → see validation result → choose installation/data locations → build → play → press F1 → adjust supported settings → close the game → launcher can update the app. Keep an offline path: after installation and a successful build, normal play must not require GitHub or a network connection.

The currently verified XEX SHA-256 is `2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7` (title `4D5707E9`, media `6153914C`, XEX `0.0.0.1`). The tracked function list belongs only to that executable. Other revisions get a clear "unsupported game version" result rather than a guessed build.

## Decisions and boundaries

| Decision | First PC implementation |
| --- | --- |
| Launcher stack | Windows WPF app on a pinned, self-contained .NET desktop runtime. Keep the game host C++/ReXGlue. Use a small launcher core library for validation, build orchestration, settings, and updating so UI logic stays thin. |
| Build model | Build on the user's PC from their own XEX. A release carries the launcher, builder, project sources/config, and a pinned, license-reviewed toolchain bundle or bootstrapper; it carries no user's XEX, assets, generated PPC output, or per-user cache. |
| Game location | Default to **use folder in place**, with an optional "copy into managed library" action. Never change the selected source folder. The chosen location is persisted per installation. |
| Installation | Per-user default under `%LOCALAPPDATA%\MKVDCU-Recomp\`. Allow a custom install root. Put binaries/builds, game copies, saves/settings, and cache in distinct directories. |
| Update channel | Stable by default; opt-in preview. Releases are selected by channel and compatibility manifest, never by arbitrary asset filename alone. |
| Menu activation | F1 by default. Controller chord is configurable, default Back+Start held for 0.7 seconds if testing shows it does not interfere with game actions. Keep F1 as a recovery binding. |
| Settings | One typed, versioned schema; launcher edits launch/build settings, in-game menu edits runtime settings. Save atomically and migrate old schemas. |

Before distributing a builder or native output, review redistribution terms for the SDK, compiler/toolchain, third-party libraries, and recompiled game code. If distribution of generated native game code is not acceptable, the local-build release model above remains the target. The update feed must publish only artifacts we are permitted to distribute.

## Experience and visual direction

The interface should feel like a neutral "realm gate" companion to the game: two opposing color fields meet at a thin central seam. Use original geometric forms, metal/stone texture, soft smoke, and a subtle scanline/light pass. Do not lift game logos, character art, fonts, audio, or screenshots into the launcher without permission. The same design language appears in the in-game overlay, with simpler motion to preserve frame time.

| Token | Direction |
| --- | --- |
| Base | Near-black charcoal `#101217`, raised graphite `#20252C`, warm off-white text `#ECE8DF` |
| Accent | Ember red `#C94B3D` on the left; electric cyan `#48B7C7` on the right; use both sparingly |
| Typography | Licensed condensed display face for headings; plain readable UI face for controls and errors |
| Shape | Tall beveled panels, clipped corners, hairline borders; avoid tiny text and dense grids |
| Motion | 150–250 ms panel transitions, optional low-motion setting; never animate build/error text |
| Sound | Optional original UI cues, off by default until volume and device behavior are tested |

### Launcher layout

```text
┌ MKVDCU-Recomp ──────────────────────────────────────── v0.x ┐
│  PLAY      LIBRARY      BUILD      SETTINGS      UPDATES     │
├───────────────────────┬────────────────────────────────────┤
│                       │  GAME READY / NEEDS VALIDATION      │
│  Realm-gate artwork   │  Source: D:\Games\MKVSDCU          │
│  and current build    │  XEX: supported retail base         │
│  status               │  Build: current / outdated / absent │
│                       │                                    │
│                       │  [ PLAY ]  [ VERIFY ]  [ BUILD ]   │
├───────────────────────┴────────────────────────────────────┤
│  Last run: clean exit  ·  Updates: checked today  ·  Logs ↗ │
└────────────────────────────────────────────────────────────┘
```

The main action changes from **Select Game Folder** to **Build** to **Play** as each gate passes. A failed gate stays visible with a short reason and a "details" expander; no opaque spinner. The Build screen shows current phase, elapsed time, per-phase log, cancel behavior, disk estimate, and a final diagnostic summary. The Updates screen shows current/new versions, channel, notes, download size, and "Install after game exits." Settings has Installation, Game data, Build/cache, Updates, and Diagnostics sections with a browse button and a reset-to-default option for each path.

### In-game Port Menu layout

```text
┌ PORT MENU ───────────────────────────────────────────── F1 ┐
│  DISPLAY   GRAPHICS   PERFORMANCE   CONTROLS   AUDIO   INFO│
├────────────────────────────┬───────────────────────────────┤
│  Display mode              │  PREVIEW / EXPLANATION          │
│  ◉ Borderless  ○ Windowed  │  What changes, expected cost,   │
│                            │  and LIVE / RESTART badge.     │
│  Window size       1280×720 │                               │
│  Render scale      1×       │  [ Apply ]  [ Restore ]        │
│  Output filter     Bilinear │                               │
├────────────────────────────┴───────────────────────────────┤
│  A Select   B Back   F1 Close   Defaults   Pending: 1 restart│
└────────────────────────────────────────────────────────────┘
```

The overlay sits on a dimmed game frame and scales for 720p through 4K and high DPI. It supports mouse, keyboard, and controller navigation, visible focus, accessible contrast, clear current/pending values, and a searchable setting list later. A controller must always be able to reach **Restore Defaults** and close the menu even after a bad remap. Settings that risk a black screen (display mode/monitor) use a timed confirmation with automatic revert. Prefer the game's own pause flow if a reliable pause/resume hook is proven; until then, opening the overlay must explicitly indicate whether the match continues and must neutralize guest controls while menu focus is held.

## First-run and build state machine

| State | Action | Exit condition / failure |
| --- | --- | --- |
| No game folder | Choose directory with root `default.xex` | A folder/permission error is shown before any copy or build. |
| Inspecting | Check `XEX2` magic, SHA-256, expected title/media/version, required directory inventory, readable files, and available disk | Show exact failed check. Fingerprint only the required set on first validation; deep asset scan is optional. |
| Supported | Choose use-in-place or managed copy; choose install, user data, and cache roots | Reject overlapping source/install/cache paths, unwritable destinations, and insufficient space. |
| Preparing | Create a per-build workspace and a manifest with validated absolute paths; verify pinned SDK/compiler/CMake/Ninja dependencies | Never invoke a shell with interpolated user paths. Use argument arrays and a working directory. |
| Codegen | Run ReXGlue codegen against the exact XEX and function config | Capture progress/log. An unmatched hash cannot enter this state. |
| Configure/compile | Run CMake/Ninja with the pinned SDK and compiler | Build into a versioned staging directory. Keep the last working build. |
| Verify | Check executable/plugin presence, binary metadata and launch prerequisites; do a non-game-launch smoke check where possible | Mark ready only after all required outputs exist. |
| Ready | Launch native game with chosen game/user/cache roots, `--gpu_plugin xenos`, audio enabled, and a unique log | Launcher tracks process exit and surfaces recent log/diagnostic on crash. |

The current scripts (`stage-game-data.ps1`, `build-pc.ps1`, `run-pc.ps1`) prove this sequence for one workspace but hard-code its XEX, SDK, and output paths. Extract the validation/build/launch rules into launcher core or a CLI that both the GUI and scripts call. Make the generated manifest and CMake configure cache specific to the selected folder, SDK revision, and host source version. Resume interrupted copies/builds only after checking fingerprints; otherwise discard the partial staging directory. Do not delete a known-good build to make room without an explicit user choice.

Build identity should be recorded as `(XEX SHA-256, host source revision, function-config revision, SDK/toolchain revision, build options, platform)`. A changed identity invalidates the appropriate generated/compiled stages; changing only a user setting should not trigger codegen. The builder's journal records each phase and result so a crash or cancelled build can be explained on next open.

## Settings contract and capability gates

The launcher writes `launcher.json` for paths, update channel, and build identity; the game writes `port-settings.toml` for gameplay-facing preferences under the chosen user-data root. Both use schema versions and atomic temp-file + rename writes. Launcher passes resolved roots and a config path to the game as arguments. Preserve Xbox saves and game data during updates and settings resets. A capability query maps each setting to **available now**, **requires restart**, **experimental**, or **unavailable**; the UI does not offer a control that has no tested implementation.

| Area | Planned control | Implementation status / rule |
| --- | --- | --- |
| Display | Windowed/borderless fullscreen, monitor, window size | SDK has `fullscreen` live callback; `window_width`, `window_height`, monitor, and `resolution` are restart settings. Test mode switches and timed revert. |
| Resolution | Internal integer render scale 1× then tested 2×/3×; output filter bilinear/CAS/spatial FSR where supported | SDK `resolution_scale` is integer 1–8 and requires restart; `present_effect` and filters need per-backend verification. Do not label output scaling as internal resolution. |
| Downscaling | Lower-cost preset for weaker GPUs | Investigate guest video mode and renderer path; current render-scale control cannot go below 1×. Hide until image, UI, and performance are verified. |
| Performance | VSync, pacing preset, measured FPS counter, optional 30/60 targets | `video_mode_refresh_rate` changes guest mode and requires restart; it is not proof of simulation-rate independence. Gate higher or unlocked FPS behind timing, gameplay, cutscene, and audio tests. |
| Graphics | Anisotropic filtering, present sharpness, safe image enhancements | SDK exposes `anisotropic_override` and CAS/FSR-related presenter options. Expose only tested values, with clear performance cost and defaults. |
| Controls | Keyboard mapping, controller button profiles, controller assignment, rumble, menu chord | Keyboard-to-pad bindings already exist. Controller remapping needs a new host input transformation before guest XInput state; preserve raw menu navigation, conflicts, and a reset path. |
| Audio | Master/music/effects where a reliable mixer API exists; output/mute if supported | Current verified launch needs `--no-audio_mute`. Do not draw sliders that cannot control separate game buses. Audio endpoint selection requires runtime support and device-change testing. |
| Advanced | Diagnostics, log path, build/XEX IDs, cache reset, safe-mode launch | Cache reset is explicit and affects cache only; safe mode restores known-good graphics/input defaults for one launch. |

For each setting, record `id`, type/range, default, underlying SDK binding or host callback, availability probe, apply phase, revert action, and test status. In-game changes first update a pending model; Apply validates the group, performs live changes on the UI thread where appropriate, then saves. Restart settings are saved with a prominent **Restart game to apply** state. A failed apply returns to the last known-good value.

## In-game integration work

1. Derive the app's menu from the existing `MkvsdcuApp` hooks: `OnCreateDialogs` for a custom `ImGuiDialog`, `OnConfigureStyle`/`OnConfigureFonts` for the visual identity, and `RegisterBind("bind_port_menu", "F1", …)` for a rebindable hotkey. Existing SDK binds use F3 (debug), Backtick (console), F4 (generic settings), and F7 (achievements), so F1 has no current collision in this checkout.
2. Add a typed adapter over ReXGlue cvars and host-specific settings. Check cvar lifecycle before applying. Avoid direct graphics object mutation from the wrong thread.
3. Add controller menu navigation and a chord detector that consumes its activation input. Prove the SDK's input-active behavior actually prevents button presses from reaching gameplay while the overlay is focused. Handle disconnect/reconnect and two controllers.
4. Add a remap layer at the host input boundary, with separate raw input for the menu and a transform for guest-visible controls. Never allow the menu activation/recovery keys to become unreachable.
5. Investigate a safe game pause path. Do not claim the overlay pauses gameplay until a full match, cutscene, and menu transition test passes.

## GitHub Releases updater

The configured origin is `KawaiiBunga/MKVDCU-Recomp`. Stable checks use GitHub's latest published full-release endpoint; preview checks list releases and select an explicit prerelease. Git tags without releases do not enter the feed. A 404, private repo, no releases, or offline state displays **No update information** and leaves Play available. The public releases page was not available unauthenticated during this planning pass, so the first published accessible release and its asset format remain a prerequisite.

Each release should include a small, versioned `release-manifest.json` with app version, channel, minimum launcher version, supported XEX hashes, SDK/toolchain revision, platform, artifact names/sizes/SHA-256, migration version, and whether codegen/rebuild is required. The release pipeline builds the launcher/builder, tests it against a local private game fixture without uploading game files, produces signed or otherwise verifiable artifacts, and publishes notes plus manifest/assets. GitHub's release asset API exposes a SHA-256 digest; compare it to the downloaded bytes and the manifest. Use HTTPS and fixed GitHub hosts, enforce size limits, reject archive traversal/symlinks, and never execute a partially downloaded file.

Update sequence: check at startup then no more than daily unless manually requested → compare channel/version/compatibility → show notes and required rebuild/disk space → download to a temp file → verify digest → unpack into a new version directory → validate contents → exit launcher/game when needed → a separate updater swaps the active version pointer → launch health check → rollback to previous version if startup fails. Keep game files, saves, settings, and logs outside version directories. If an update changes the function config or SDK/host ABI, mark the existing native build stale and rebuild locally against the user's validated XEX before Play. Failed updates leave the old launcher and build usable. Use conditional requests/ETags and bounded retries to respect API limits.

GitHub API references: [release listing and latest release](https://docs.github.com/en/rest/releases/releases), [asset metadata and digest](https://docs.github.com/en/rest/releases/assets), [REST API conditional request guidance](https://docs.github.com/en/rest/guides/best-practices-for-integrators).

## Repository implementation map

| Proposed path | Responsibility |
| --- | --- |
| `launcher/MKVDCU.Launcher/` | WPF screens, theme, accessible navigation, UI state |
| `launcher/MKVDCU.Core/` | Folder validator, settings schema, paths, build state machine, release/update service |
| `launcher/MKVDCU.Build/` | CLI over the same core for developers/CI; replaces hard-coded script logic |
| `launcher/MKVDCU.Updater/` | Small separate process that stages, swaps, checks, and rolls back app versions |
| `targets/mkvsdcu/private/rexglue-host/src/port_menu/` | ImGui overlay, capability adapters, input transform, visual resources |
| `config/supported-builds.json` | XEX fingerprints and compatible function-config/build IDs, with no game data |
| `docs/LAUNCHER_AND_PORT_MENU_PLAN.md` | This plan; update statuses as milestones land |

Keep PowerShell scripts as developer wrappers around the CLI during transition. The repo's ignored `user-game-files/`, generated PPC output, SDK checkout, build output, cache, and logs stay untracked. CI can compile the launcher and host scaffolding and test validator/update fixtures without real game files; full game smoke tests stay local.

## Milestones and acceptance gates

1. **Path-independent build core.** Select a game folder outside the repo, validate the exact XEX, generate/configure/build/launch into chosen paths, and report every phase. A path with spaces and Unicode must work. Wrong XEX and missing assets fail before codegen.
2. **Launcher first run.** Fresh Windows account can install the self-contained launcher, browse a folder, choose locations, build, play, reopen, and preserve saves/settings. Cancelling or losing power during build leaves prior build intact.
3. **Port menu shell.** F1 and controller chord open/close a styled overlay; it remains navigable at 720p/4K, captures input correctly, and can recover defaults. No gameplay-input leak during focus.
4. **Verified settings.** Add display/window, 1×/tested higher render scales, tested present filters, VSync, and keyboard mappings with live/restart badges and persistence. Record performance and visual regressions before adding a preset.
5. **Controls and timing.** Add controller remapping and a robust reset path. Measure simulation and audio at baseline; expose any FPS targets only when a full-match timing matrix passes. Investigate downscaling and separate audio volume controls before enabling them.
6. **Updater and release pipeline.** Publish a test release, exercise stable/preview/offline/404/rollback flows, verify asset digests and compatibility, and prove an update preserves imported game files and saves. Then run a fresh-machine end-to-end test.

For every milestone, capture the exact SDK revision, XEX hash, GPU/controller, logs, and a brief result. Re-run an entire Arcade match after changes to runtime input, render scaling, or timing. The launcher can ship a narrower feature set once the first-run/build/play/update loop is reliable; unavailable enhancements remain visibly tracked in this plan.
