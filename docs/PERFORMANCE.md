# Performance notes

Measurements and optimisation leads for the PC port. Numbers come from the F2
overlay and its CSV log (`perf-*.csv` in the user logs folder), taken on the
development PC: GeForce GTX 1650 SUPER, 12 logical CPU threads, 1920x1080 and
144 Hz displays, windowed 1280x720, D3D12, render scale 1x.

## How to measure

- Press F2, then turn on **Detailed panel** in F1 > Performance. The panel
  shows the game's real frame rate (timed at its `VdSwap` call), frame-time
  percentiles and hitches, per-thread CPU, the busiest GPU engine, VRAM and a
  "what is limiting it" line.
- F1 > Performance > **Start logging** writes one CSV row per second. Compare
  settings over the same stretch of play (for example a full Arcade round).

> **Build-flags warning (fixed 2026-09-23).** The development build directory's
> CMake cache had lost `CMAKE_CXX_FLAGS_RELEASE`, so it compiled the whole
> recompiled game at `-O0`. The launcher's own build folders were unaffected
> (`-O3 -DNDEBUG`). The host `CMakeLists.txt` now restores the flags with a
> warning. The baseline, timer A/B and first profiles below were taken on that
> `-O0` build; see "Optimised build" for the corrected numbers.

## Baseline (2026-09-23, -O0 development build)

| Scene | Game FPS | Avg / 1% frame | GPU (busiest engine) | Busiest threads |
| --- | --- | --- | --- | --- |
| Title screen | 60 | 16.7 / ~19 ms | ~40-55% (`graphics_1`) | - |
| Arcade fight, Scorpion vs Joker | 60 | 16.7 / 19.4-24.5 ms | ~90-94% | Main XThread 94-99%, XThread1BAC 93-97% of a core |

The game holds 60 FPS on this PC, but with little headroom on both sides: the
GPU is ~90% busy at 1x, and two guest threads each use nearly a full core.

## Timer resolution A/B (2026-09-23, -O0 development build)

`scripts/bench-pc.ps1`: title screen, 180 s per run, last 60 s summarised,
alternating runs.

| Run | FPS | p99 | Worst | Jitter | Hitches/min | Busiest thread | GPU |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Timer off #1 | 59.38 | 31.9 ms | 232 ms | 3.11 ms | 14 | 95% | 51% |
| Timer on #1 | 59.28 | 31.1 ms | 225 ms | 3.14 ms | 20 | 96% | 50% |
| Timer off #2 | 59.10 | 36.9 ms | 229 ms | 3.85 ms | 20 | 96% | 51% |
| Timer on #2 | 59.00 | 37.5 ms | 191 ms | 3.82 ms | 23 | 95% | 51% |

Result: no measurable difference. Variation between runs of the same setting
is larger than on vs. off. An earlier 150 s pass that looked better with the
timer on included startup, so it was noise. The option stays on because it
costs nothing here.

The 14-23 "hitches" per minute turned out to be attract-mode asset loads (see
below), not stutter.

## Optimised build (2026-09-23, -O3)

| Scene | -O0 dev build | -O3 build |
| --- | --- | --- |
| Intro movies | 5-11 FPS, decoder thread at 100% of a core; intros end at ~71 s | 30-34 FPS (the movies' own rate); intros end at ~23 s |
| Title screen / attract, locked | 59.1-59.4 FPS | 58.5-59.5 FPS, p99 29-38 ms |
| Title screen / attract, unlocked timing (`--no-vsync`) | - | **129-132 FPS**, GPU 95% |

What the numbers mean:

- **This PC is GPU-bound, with lots of CPU headroom.** Unlocked, the game runs
  at more than twice the console rate and stops at the GPU (GTX 1650 SUPER at
  1280x720, 1x).
- **The "95% busy" guest threads are mostly waiting.** CPU profiles at 60 FPS:
  - Main XThread: ~75% of its samples are in `KeDelayExecutionThread` → `XThread::Delay` → `ZwYieldExecution`/`SwitchToThread`. The game asks for short delays while it waits for the next frame, and the SDK yields rather than sleeping. Real guest work is ~25% of a core.
  - The render thread: ~95% is in `sub_827F60C0` / `sub_827E1410`. This is the game's Direct3D waiting for room in the GPU command ring, spinning on the read pointer with a 5 s watchdog.

  So per-thread CPU cannot show a CPU bottleneck in this game; use the profiler.
- **The title-screen "hitches" are loads.** The title screen switches to attract-mode demo fights about every 33 s. During the 3-4 s transitions VRAM jumps (363 → 560 MB), the working set grows by 300 MB and frames reach 80-230 ms. They are not steady-state stutter.
- The overlay's sampler used a system-wide Toolhelp snapshot every second (~5% of a core in `ZwUnmapViewOfSection`). It now keeps handles to the game's own threads and re-enumerates every 5 s.

## Finding hot guest code

F1 > Performance > **Profile the 4 busiest threads (5 s)** samples each busy
thread's instruction pointer about once per millisecond and maps it to the
recompiled guest function (`sub_XXXXXXXX`) or host symbol it falls in. The
report shows in the menu and is saved as `profile-*.txt` in the logs folder.
For unattended runs, `--port_profile_after=<seconds>` starts the same profile
automatically after launch (with `scripts/bench-pc.ps1 -GameArgs`).

## Graphics APIs

The SDK ships two GPU backends: Direct3D 12 (default) and Vulkan. Both are
compiled into the port and selectable in F1 > Graphics > Graphics API.
Direct3D 11, OpenGL and OpenGL ES have no backend in the SDK. Adding one would
mean porting the whole Xenos command processor, render-target cache and shader
translator, so it is out of scope. Vulkan covers the cases where OpenGL would
help, such as driver workarounds.

## Frame timing

- The simulation advances once per presented frame. With the guest vblank
  unlocked (F1 > Performance > Game timing) the game rendered 72-73 FPS and
  the in-match round clock ran ~27% fast (14 game seconds in ~11 real
  seconds). A real high-frame-rate mode needs the game's update step
  decoupled from vblank, which is engine work.
- Presenter-side frame interpolation is not possible: the presenter receives
  finished frames with no motion vectors or depth history.
- The SDK's `CommandProcessor::counter()` rises on every vblank *and* every
  swap; the overlay subtracts swaps to report the true vblank rate.

## Leads and fixes

Status: **done** (in the port), **measure** (implemented as an option, needs a
comparison run), **idea** (not implemented).

| Area | Change | Status | Expected effect |
| --- | --- | --- | --- |
| Frame pacing | Request 1 ms Windows timer resolution at startup (F1 > System > High-precision timer). The SDK's guest vblank thread sleeps `Sleep(1)` per tick and never raises the timer, so at the default ~15.6 ms resolution vblanks arrive in catch-up bursts. | done | Measured: no difference (see A/B above); the guest vblank rate is already 60.0 Hz either way |
| Scheduling | Process priority above normal and EcoQoS power throttling off by default (F1 > System). | done | Fewer delays from background programs and Windows 11 efficiency cores |
| Build | Restore `-O3 -DNDEBUG` when the CMake cache has lost it (host `CMakeLists.txt`). | done | Intro movies 5 → 30 FPS on the affected build; protects every future build |
| CPU | Keep CTR, XER, CR and reserved PowerPC registers in C++ locals (`config/mkvsdcu_codegen.toml`). The host compiler can then keep them in registers instead of reloading the context struct. `non_argument_as_local` crashed at startup and stays off. | done | Stable over a 6-minute attract soak; main-thread idle share rose from 76% to 79%; unlocked 129-132 → 133 FPS (GPU-bound, within noise). Matters most on slower CPUs |
| CPU | The game waits for frames with `Sleep(0)` loops (the main thread) and by spinning on the GPU ring read pointer (the render thread), so two cores stay busy even with headroom. A short real sleep in `XThread::Delay`, or a midasm hook adding a pause to `sub_827E1410`, could save power and free cores for SMT siblings. | idea | Lower power and heat; no FPS change expected on 6+ core CPUs |
| CPU | `non_volatile_as_local` and `skip_lr` codegen options. | idea | Larger CPU gain; needs the game's setjmp/longjmp addresses mapped first |
| GPU | Render target path: the SDK picks host render targets (RTV) on NVIDIA/AMD; ROV is "currently much slower" per the SDK. Exposed as F1 > Graphics > Render target emulation. | done | Keep Automatic unless debugging |
| GPU | Occlusion queries, resolve readback, memexport readback and per-frame page-state refresh are now switchable (F1 > Graphics > Accuracy). | measure | Each can cut GPU/CPU sync; check visuals |
| GPU | Vulkan backend compiled in (F1 > Graphics > Graphics API). | measure | Driver-dependent; compare with the CSV log |
| GPU | AMD FidelityFX (CAS, FSR 1/2/3) compiled in (F1 > Graphics > Upscaling filter). FSR 1 at 1x internal scale behaves as a sharpener; pair it with a window larger than the render. The SDK labels FSR 2/3 experimental: it has no real motion vectors or depth, so it synthesizes them and may fall back to spatial FSR. | done, measure | Sharper upscaled output for little cost |
| Overlay | With any ImGui dialog open the presenter repaints at the monitor rate plus the game's rate (205 Hz on a 144 Hz display). Keep the overlay off when measuring pure GPU cost. | known | Small GPU cost while the overlay is open |
| Stability | Recompiler missed functions only reachable through vtables; `scripts/find-function-seeds.py` scans the loaded image and added 154 entries (including the `0x826AF018` crash). | done | Fewer "unregistered function" crashes in untested paths |
| Stability | Launching the game minimized crashes inside `rexruntime.dll` during startup (SDK issue). The launcher never starts it minimized. | known | - |
