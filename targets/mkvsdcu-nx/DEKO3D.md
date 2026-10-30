# MKvDCU native deko3d build

This target connects MKvDCU's XDK 2.0.7645 D3D shader identities and draw calls
to a native deko3d backend. The guest still generates PM4 state; the native
frontend consumes it. The native build does not load Xenos, Vulkan or Mesa/NVK.
The previous Vulkan build remains a separate target and artifact.

## Local package

The verified archive is `dist/mkvsdcu-deko3d.zip`. Extract its `mkvsdcu/`
folder into `sdmc:/switch/`, then add your existing game data as described below.

The completed local package is written to `dist/deko3d/`. Copy these together
to `sdmc:/switch/mkvsdcu/`:

* `mkvsdcu.nro`
* `mkvsdcu_shaders_dk.nfsp`
* `mkvsdcu_shadercache_dksh.bin`
* `mkvsdcu_shader_limits.bin`
* `mkvsdcu.toml` (native defaults; back up an existing configuration first)

Keep the existing `game_root/` beside the NRO, or the game's ISO in that
directory. Use full application memory through title takeover. The shader
library and cache are generated from the user's own game; do not redistribute
them. The build and shader verification JSON files record local hashes.

The native renderer writes newly compiled variants to
`cache/mkvsdcu_dksh.bin`. The offline cache is verified in bounded chunks and indexed on disk; only the
requested DKSH is read into RAM. Newly compiled variants stay in the separate
writable cache. Its compiler identity includes the fixed ABI revision,
the eight patched uam fixes, SPIRV-Cross version and runtime GLSL revision.
An incompatible cache is rejected. A missing shader library stops GPU setup.

## Build on Windows

Run `scripts/build-switch-deko3d.ps1 -Python <existing-python.exe>` from
PowerShell. It uses tools already installed under `E:/tools`, prepares the
separate `references/mkvsdcu-deko-sdk`, builds with GCC 16, hook-safe direct
calls and LTO, checks the ELF/NRO and stages the NRO. It does not install tools
or use Docker/WSL. Pass `-ShaderPackage <verified-package-directory>` to stage
the accompanying shader files.
The script selects the existing LLVM-MinGW bundle's GNU Make for GCC's
parallel LTO jobs, with the same job limit as `-Parallel`.

Shader generation uses the static census from `audit-switch-shaders.py`, the
MIT translator in `tools/mkvsdcu-fixed-shaders`, then
`compile-fixed-switch-spirv.py`, `compile-native-switch-dksh.py` and
`package-native-switch-shaders.py`. All 19,340 original containers are required:
1,703 vertex and 17,637 pixel. The native GLSL converter is the exact source
used by the runtime. Each offline DKSH is compiled twice and compared.
`mkvsdcu-native-library-check` exercises the production library decoder,
container identification and cache reader, including a corrupted-cache check.

## Verified local build (2026-10-21)

The initial native Windows build produced a 73,246,917-byte game NRO. ELF/NRO checks
passed: entry point zero, packaged NRO0/ASET headers, native deko3d symbols,
all 11 strong shader/draw hooks and zero direct-call bypasses of those hooks.
No Vulkan/NVK driver entry points were linked.

All 19,340 shaders passed SPIR-V conversion, the runtime GLSL converter and
two identical native DKSH compilations with zero failures or diagnostics.
The production reader verified 15,474 deduplicated cache entries covering
every original shader and rejected a corrupted cache. The largest measured
scratch requirement is 153,088 bytes per warp; the packaged queue budget is
218,624 bytes per warp. These are offline checks; console behavior remains
unverified by those checks. The manifests record the current NRO's size/hash.

The first console test reached native device, shader library/cache, presenter
and game startup, then crashed in `AudioSystem::SubmitFrame` at NRO offset
`0x12ad50`. Its driver pointer was null. MKvDCU can retire an audio client
inside a callback and still submit the pending frame. The SDK patch
`audio-client-lifetime.patch` rejects zero/invalid handles, guards inactive
slots in release builds, makes retirement idempotent and publishes clients
before waking their worker. A host regression using the production function
bodies reproduces the unpatched access violation and passes after the patch,
including concurrent submission/retirement. The user confirmed the patched
build reaches fights and improves performance, but reported incomplete graphics.

The same SDK patch also synchronizes callback execution with client retirement.
The later splash-screen stack capture showed the main thread joining an audio
engine worker while that worker waited for another wake-up. Retirement previously
returned while an audio callback could still update the engine's busy flag.
It now waits outside the SDK global lock for in-flight callbacks to finish.
Self-unregistration remains supported, and its wrapped argument is freed after
the callback returns. The production-body regression covers this interleaving,
callback completion requiring the global lock, self-retirement, and repeated
dispatch/retirement races; removing the synchronization makes the test fail.
This startup correction still requires console confirmation.

### Console rendering correction (2026-10-22)

A captured fight frame contained 190 draws, including 55 skipped draws using
utility programs absent from the static asset census. Seven small live programs
were captured during startup and gameplay. Their instruction bytes are preserved
exactly, including the patched vertex-fetch formats. Captured container constants
and interpolator declarations are retained for the depth-copy pair; its baked
register 255 must not be read from the pixel constant buffer. Utility vertex
fetches read the actual GPU fetch constants without relying on a queued D3D object.

The supplemental programs pass SPIR-V/GLSL translation and two identical DKSH
compilations each. The merged package contains 19,347 shaders and 15,481 native
cache entries. The production identity matcher recognizes all seven captured
programs exactly. The production library/cache reader verifies full coverage,
scratch limits and corrupted-cache rejection. The queue scratch budget remains
218,624 bytes per warp.

The native draw frontend also accepts memory-export vertex shaders without a
pixel shader. All 64 static memory-export programs pass the production eligibility
check. Color resolves now replace invalid destination formats with the source
surface's format; 1,024 combinations match the PC reference. In particular,
destination format zero previously rejected repeated game texture copies.
The temporary shader capture option defaults off. Console visual verification
of these rendering corrections remains necessary.
The corrected 73,259,205-byte NRO passes native symbol/header checks, all 11
strong shader/draw hooks and zero direct-call bypasses. Its exact hash is in
`switch-build-verification.json`.

The user subsequently reported that only UI remained visible. New console logs
confirmed the utility library was loaded and resolves no longer rejected formats.
The captured scene clears use a 520-pixel 4x MSAA depth surface, then draw through
a 1040-pixel 2x MSAA view at the same EDRAM base. Both use the same 13-tile sample
pitch. Independent host images had lost that clear; the scene's greater-or-equal
depth test could then reject everything against the initial depth value of one.

Uniform XDK depth clears now transfer between compatible surface views in physical
sample coordinates. MSAA mode participates in depth-image/pass identity. Replay
requires the exact clear programs, unconditional depth/stencil tests and coverage
of the scissor; later nonuniform depth/stencil writes invalidate the replay record.
49,473 pitch/sample cases match the SDK's tile conventions. A production-function
test with captured microcode verifies the padded 520x720 allocation's 520x624 clear
becomes a 1040x624 clear, including stencil, and rejects stale/partial/discarded
clears. Console verification of this update remains pending. This correction does
not establish general nonuniform EDRAM surface aliasing.

Diagnostic-only `mkvsdcu_diag_frame_interval_s` captures at most 16 frames at the
requested interval; zero (the default) disables it. Setting
`nfsmw_nativo_diag_vertices_ps = "*"` includes all draws' vertex diagnostics in those
frames. These options avoid relying on a single startup timer for a fight capture.

## Applied techniques

The GPL NFSMW backend and its provenance are in `thirdparty/nfsmw-deko3d`.
MKvDCU adapters replace its game addresses and shader-container layouts.
The native ABI adds COLOR2, indexed/raw vertex fetch, memory export, explicit
texture instruction/filter bindings, unsigned/signed views, texture gradients,
1D helper mapping, exponent/gamma adjustment and floating-depth conversion.
The resource ABI is shared by the translator, packager and renderer. Rectangle
lists expand three corners to four in a native geometry shader, preserving the
live interpolators and choosing the diagonal/cyclic order used by the SDK's
Xenia reference. The PC trace observed 2,152 rectangle-list calls; 15 geometry
interface variants compile twice identically with no uam diagnostics. A CPU
check covers all six corner permutations and linear interpolation.

Generic optimizations retained from the documented backend include state and
constant deltas, exact geometry caches, fenced upload slots, delayed descriptor
reuse, compressed render/depth surfaces, ZCULL invalidation, deferred clears,
compiled shader variants and a sleeping compiler worker. The worker uses an
8 MiB stack and Switch priority 0x3B. Command chunks keep 256 bytes of slack;
vertex bindings avoid deko3d 0.5's multi-binding reservation issue. Shader code
storage grows in 16 MiB pages. Ring waits wake on progress with a bounded
timeout. Sampled geometry/texture hashes and NFSMW-specific pass shortcuts are
disabled. No other game's PGO profile or register-local whitelist is used.

## Validation limits

Offline shader compilation and a successful NRO build establish a native
artifact, not visual parity or a measured frame-rate improvement. No Switch
was available during initial packaging. Subsequent console tests verified boot
and entering fights after the audio correction, and exposed incomplete graphics.
The first depth-alias revision still showed only UI: console logs exposed a
520x624 clear rectangle with a padded 520x720 scissor, which the full-scissor
guard rejected. Recognition now intersects the clear geometry with the scissor;
the user subsequently confirmed that this restored the scene and audio.
The intermittent startup stall was later captured with native thread stacks:
the main thread joins an audio-engine worker that remains asleep after the
shutdown wake-up. The callback-retirement synchronization correction above
passes host regressions and awaits console confirmation.
Checks remain necessary for
movies, menus, character selection, fights/floors/shadows/effects, transitions,
two-player rendering, saves, suspend/resume and extended play.

The padded-scissor revision was subsequently tested on the Switch: the user
confirmed that the scene and audio are visible/working. Gameplay performance
and stability are still open. A repeated freeze at 81 seconds on the fight
timer was traced to the render thread waiting for a pixel specialization behind
long queued vertex compilations. Twenty exact GLSL requests were captured and
compiled twice on the PC. The next fight captured twelve more requests, including
a different output mask for the same large vertex shader. The merged package
includes all 167 observed runtime variants, for 15,648 offline cache entries,
and passes the production cache
loader and scratch-budget checks. The updated runtime cache was uploaded and
read-back verified; completing the same fight remains a console verification
gate. Missing GLSL requests are saved before compilation for further offline
cache preparation. `mkvsdcu_fast_texture_hash` selects full-byte
ARM CRC content fingerprints (with a scalar startup self-test); false retains
XXH3. Shader identities and persistent caches continue to use XXH3. This CPU
change passed its Switch startup self-test; comparable performance measurements
and longer stability testing remain necessary.

`mkvsdcu_runtime_optional_compiles` defaults to false. Cached optimizations are
always used first. Missing output-trim and early-depth variants save their exact
GLSL once for PC compilation and render through the equivalent cached shader.
This keeps a long optional uam compile from blocking a newly required shader on
the Switch's single compiler worker. Required semantic specializations still
compile when absent. The default takes precedence over the older
`nfsmw_dk_compilar_en_fondo` scheduling option. Setting the new option to true
restores runtime optimization compilation for development.

In particular, validate MKvDCU's tiled/MSAA surface aliases, depth/stencil and
gamma against the existing PC FSI reference. Less common signed packed texture
formats, compressed 3D textures, array resolves and exact floating-depth
rounding are not established by the static shader census. Missing runtime
shader identities or unsupported draws are diagnostics to investigate, not
evidence of a complete frame. Hardware performance tuning and title-specific
PGO require measurements of this renderer on this game.

The limits file identifies both the compiler and original shader library. Queue
scratch is sized from the maximum measured DKSH requirement plus a 64 KiB
margin, rounded to deko3d's alignment. Shader loads verify that budget before
binding. deko3d allocates scratch across resident warps, so using a measured
budget also bounds total allocation; see the [pinned queue work-buffer
implementation](https://github.com/devkitPro/deko3d/blob/v0.5.0/source/queue_workbuf.cpp).
A missing or mismatched limits file selects a conservative 1 MiB per warp.


## Port menu and scene resolution (v9)

Press **+ and - together** to open or close the port menu. Use the D-pad to
navigate, L/R to change tabs, and the displayed A/B prompts to select/back.
Display offers 100%, 75% and 50% scene resolution, a 60/30/uncapped rendered-frame
cap, and a compact FPS counter. Controls offers letter/position face-button
layouts, stick deadzone and swapped sticks. Audio includes mute.

Scene resolution applies after Save settings and restart. It reduces the
1040x624 3D scene to 780x468 or 520x312, retaining full-resolution composition,
game UI and port menu. Other targets retain their original resolution. The cap,
counter and controls apply immediately. Saving retains unrelated TOML values.

The current 30 FPS cap does not implement full-speed 30 FPS gameplay. The
source-backed plan for the CPU/GPU work toward 60 FPS is
[the performance roadmap](../../docs/SWITCH_PERFORMANCE_ROADMAP.md).

Cvars: `mkvsdcu_scene_scale` (100/75/50), `mkvsdcu_fps_limit` (60/30/0),
`mkvsdcu_show_fps`, `input_xbox_layout`, `mkvsdcu_stick_deadzone` (0-40 percent),
and `mkvsdcu_swap_sticks`. Keep `video_mode_refresh_rate` at 60.

## Experimental gameplay timing (v10)

The separate **Experimental 30 Hz gameplay timing** option updates the engine's
verified fixed-step delta and accumulated clock together and enforces 30 FPS
pacing. Save and restart to apply it. It is disabled by default. Combat, timer,
physics and transition behavior are not validated. Hardware testing found combat
still running at about half speed near 30 FPS. Leave it disabled for the current
60 FPS performance work. The original
frame cap remains available independently. `mkvsdcu_30fps_timing` controls the
mode; `mkvsdcu_timing_diagnostics` enables sparse timing reports in original mode.

## Draw CPU optimizations (v11)

The fixed shader path now caches complete texture binding results, including
signed views, sampler overrides, control words and inverse dimensions. It retains
the existing texture-check expiry and invalidates on image generation or GPU work
changes. Cubemap refreshes and pending/stale front-buffer lookups keep their full
path. Initial and periodic verification compares against that path; any mismatch
uses the fresh result and disables caching for the session.

Single-range raw vertex buffers use the existing geometry store, retaining guest
byte order, bounds and zero guard padding. Content checks, completion epochs and
GPU fence ownership remain in force. Multi-range and memexport draws retain the
original upload path. CPU stage timing samples approximately one in eight draws;
the log labels the estimates and reports how many draws were actually timed.

Settings for controlled comparisons (restart after changing):

* `mkvsdcu_texture_binding_cache = true`; false restores uncached preparation.
* `mkvsdcu_texture_cache_verify = false`; true checks every hit.
* `mkvsdcu_raw_vertex_cache = true`; false restores all raw ring uploads.
* `mkvsdcu_draw_timing_interval = 8`; 1 times every draw, 0 disables draw timers.

Host equivalence tests and native build checks do not establish a console FPS
gain. The research decisions and hardware comparison protocol are in the
performance roadmap linked above.

## Forward-only vertex shader variant (v14)

GPU profiling identified one skinning vertex program accounting for roughly
509 ms of a 527 ms frame during the severe slowdown. Its generated dispatcher
contains only forward branches. The offline rewrite preserves those branches
and arithmetic while removing the synthetic loop: the measured masks compile
with 78-84 registers and zero scratch, instead of 255 registers and heavy scratch.

The renderer selects this program only from its versioned offline cache keys,
for the exact original shader fingerprint. Missing entries retain the original
path. `mkvsdcu_acyclic_vertex_shader=false` and restart disables the selection.
Native build checks and CPU SPIR-V equivalence tests pass; console speed and
visual validation are still required. See the performance roadmap for the
remaining 60 FPS work.

## MK9-derived CPU comparison build (v16)

Build with `scripts/build-switch-deko3d.ps1 -CpuOptimizations -Pgo off` to enable
the staged CPU passes. They shorten verified atomic lock regions, add polling
barriers before relaxing ordinary RAM accesses, use vector float loads, and keep
trap handling out of hot code. MMIO, guest atomics and unrecognized lock regions
retain their original behavior. This option changes the NRO, not the TOML.

The detailed source audit, checks and remaining work are in
[the MK9 comparison](../../docs/SWITCH_MK9_AUDIT_20261029.md). Console correctness
and performance testing are required. The older NRO remains the rollback; a PGO
capture must use a fresh source identity after these changes.
