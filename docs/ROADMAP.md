# Roadmap

The [launcher and port menu plan](LAUNCHER_AND_PORT_MENU_PLAN.md) defines the end-to-end first-run, build, overlay, and GitHub Releases update flow. The milestones below remain the gameplay and settings work within that plan.

## In-game port menu

First make the PC build accept a user-selected game folder and install location, as specified in the launcher plan. Then build one menu owned by MKVDCU-Recomp for settings that make sense during play. The menu should be reachable by controller and keyboard, explain when a setting applies, and persist choices in the user data directory. The game should start with known-good defaults when no config exists or a saved value is invalid.

The first PC pass should cover:

| Area | Initial controls | Validation |
| --- | --- | --- |
| Frame rate and pacing | Tested presets and a clear indication of game-speed or animation limits | Match timing, input response, cutscenes, and audio synchronization |
| Resolution and display | Window size, fullscreen mode, internal render scale where supported | Menu/UI legibility, aspect ratio, presentation and GPU memory |
| Visual enhancements | Explicit toggles for supported filtering, scaling, and post-processing | Side-by-side scene checks and performance impact |
| Controls | Controller/keyboard mappings, sensitivity where relevant, button prompts | Menu navigation and full match control, including disconnect/reconnect |
| Audio | Device or output choices only where the runtime exposes a reliable API | Music, effects, voice, latency, and mute behavior |

Implement the menu behind a small configuration and capability layer. Keep PC-only renderer settings out of game logic, and expose Switch options only after the Switch backend can actually apply them. Settings that require a renderer restart should say so before saving. Avoid adding controls whose underlying runtime path has not been measured or tested.

## PC stabilization

- Exercise more Arcade paths, characters, stages, movies, menus, pause/resume, saves, and long sessions.
- Classify crashes by exact log signature. Add only observed indirect targets for the staged retail XEX; investigate renderer or runtime faults separately.
- Measure frame pacing and audio synchronization before offering frame-rate presets. A higher presentation rate must not silently speed up game simulation.
- Keep game assets and generated code outside Git.

## Switch port

The libnx toolchain test is already separate from the game. The full port still needs the ReXGlue runtime ABI, memory/thread services, filesystem mapping, controller input, audio, and a compatible Xenos graphics backend on Switch. The PC menu/configuration work can be designed for reuse, but no Switch game build is claimed today. See [Switch research](switch/RESEARCH.md).
