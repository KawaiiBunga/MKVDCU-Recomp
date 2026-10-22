# MKvDCU native deko3d migration

## Scope and current evidence (2026-10-21)

Requested result: an MKvDCU NRO which draws the game through deko3d, with the
applicable optimizations from the SA2 and NFSMW ports. A successful cross link
alone does not establish rendering correctness or console performance.

Tools, downloads, caches and temporary installation files must live under
`E:/tools`. Use native Windows executables; Docker, WSL and a restart are not
part of this build. Keep the existing PC renderer as the visual reference.

The previous Switch target statically links ReXGlue's Xenos Vulkan emulator,
including FSI EDRAM emulation, Mesa/NVK, glslang and SPIRV-Tools. Setting a cvar
to "deko3d" cannot replace that backend. The legacy app path explicitly calls
`LoadGpuPlugin("xenos", "vulkan")`. Its verified PC FSI configuration restores
floors absent from its FBO path; native depth, stencil, resolve and surface
aliasing therefore need explicit visual checks.

The user's executable has been read locally without modifying it. Its XEX
library table identifies D3D9, D3DX9, XGRAPHC and XBOXKRNL as **2.0.7645**.
That is the original N&B renderer's XDK, not SA2's 2.0.21076. The existing
MKvDCU ring hook uses fence pointer +10896 and flags +10941, agreeing with
the original layout. This is evidence for starting from the original layout;
it does not prove every resource, hook signature or private offset.

The decoded image is 18,022,400 bytes at 0x82000000. Initial structural scanning
finds 68 shader containers in it. The game's 708 Asset files total
1,618,673,747 bytes. Shader coverage must include their compressed payloads
and runtime-generated or patched shaders, not just these 68 built-in shaders.
All images, dumps, converted shaders and caches remain ignored local output.

## Sources studied and pinned

* [SA2 docs](https://github.com/ChanseyIsTheBest/sa2-nx/tree/adf3a4eee524dcca5cd7c1b42454ddb8a09124a1/docs),
  especially deko3d-renderer-guide, renut-renderer, api-renderer-assessment,
  api-renderer, porting-guide, cpu-rounds, building and switch-testing.
* [NFSMW deko3d guide](https://github.com/StevensND/nfsmw-nx/blob/266f943263993148e49e968208a4f784ddd8d88d/docs/HowToDeko3d.md)
  and [devlog](https://github.com/StevensND/nfsmw-nx/blob/266f943263993148e49e968208a4f784ddd8d88d/docs/deko3d-devlog.md).
* [NFSMW main docs](https://github.com/StevensND/nfsmw-nx/tree/be2d4c67ab42d6d629746a01dbf03cef824c8d2d/docs),
  especially porting-another-game, native-renderer, shaders, toolchain,
  measuring, platform-notes, audio-and-video, Mesa and performance-history.

Exact snapshots are in local `research/deko3d-sources/`. The SA2 snapshot
includes plume's MIT deko3d backend, the MIT XenosRecomp fork and patched uam.
The latter has **eight** patches, including the block-transition stall fix;
the older NFSMW guide lists only seven. Use the complete pinned patch set
and fingerprint it in DKSH cache identities.

## Architecture decision

Use the pinned GPL NFSMW PM4 frontend and native deko3d backend, with
MKvDCU-specific registration, binding and draw hooks. Keep the game's original
FlushState packets. The 7645 shader object offsets and draw signatures were
checked against this executable; NFSMW addresses and gameplay hooks are not
installed. The isolated SDK is `references/mkvsdcu-deko-sdk`.

The original P0 audit established compiler feasibility using the MIT reNut
shader ABI. It did not establish a reusable renderer, and its successful DKSH
files are not runtime inputs. The implementation now has a separate fixed ABI:
112 shared uint4, COLOR2, raw/indexed vertex fetch and memory export, dual signed
texture views and per-instruction texture/filter bindings. Its version-4 library
metadata, translator, runtime and cache identity agree. Validate the new full
pipeline separately from the old P0 reports.

The reNut renderer has no license grant for this title and is not imported.
The imported GPL renderer and MIT shader helpers retain licenses and pinned
provenance. Other games' addresses, shader substitutions, PGO profiles and
sampled-data assumptions are not MKvDCU optimizations.

Do not wholesale update the existing SDK or replace the existing vendor tree.
Use isolated build directories, record imported revisions and patches, and
preserve pre-existing user edits. Strong hooks must reach the executable as
objects and must be protected from direct-call staging. Inspect the final ELF
to prove every required hook survives and no Mesa driver is linked in a native
deko3d release.

## Execution order and acceptance gates

1. **Build prerequisites and census.** Restore portable CMake/Ninja, devkitA64
   GCC 16, newlib, rules, libnx 4.12, deko3d 0.5, patched uam, DXC and
   SPIRV-Cross. Read the user's XEX and construct an instruction/call catalog.
   Identify CreateDevice, KickOff/segments, waits/fences, Swap, draw variants,
   ClearF, Resolve, shader registration, vertex declarations, destroy and
   command-buffer/tiling calls. Record signature, callers, offsets and evidence
   for each. Unconfirmed candidates are never installed as hooks.
2. **Shader P0.** Extract containers from the image and decompressed assets;
   record stage, full container hash, source and offset. Translate through
   XenosRecomp -> HLSL -> DXC -> SPIR-V -> SPIRV-Cross -> GLSL -> patched uam
   -> DKSH. Validate every stage and report failures and warnings. Fixed texture
   bindings, UBO layouts, bool/alpha/depth variants and used VS outputs must
   agree with the renderer. No unbound descriptors, Int64 buffer addresses or
   Vulkan-only bindless constructs may reach uam. Test repeated compiles for
   deterministic bytes. Build variants from MKvDCU's own observed states.
3. **PC native reference.** Wire the API renderer behind an explicit boot
   choice, keeping the Xenos reference. Original state setters maintain guest
   shadows; render commands own snapshots and upload data until consumed.
   Fences complete only when that lifetime is safe; callbacks run in order.
   Compare videos, title/menu, character selection, fights (including floors,
   shadows, effects and transitions), cutscenes and two-player rendering.
   Verify gamma, stencil, MSAA/tile aliases, scissor and memexport behavior.
4. **Switch first light.** Create the deko3d device/queue, debug callback,
   bounded memory pools, work slots/fences, native presenter and settings
   overlay. Test orientation, depth and uploads before real draws. Use positive
   viewport sizes; shader Y convention and culling must agree. Read the default
   NWindow; do not open a second display. Preserve normal-speed guest pacing
   and real guest-frame telemetry. A native renderer must not load Xenos.
5. **Parity and no-driver build.** Replay the same native draw data on deko3d.
   Shader compile worker: one thread, 8 MiB stack, priority 0x3B, sleeping job
   queue, joined shutdown, startup self-test. Persist compiler-versioned DKSH
   and observed variant lists with fsync. Do not turn missing shader draws into
   a silently blank but "successful" frame. Count misses and unsupported work.
   Package only after native symbols, absence of NVK and NRO/ASET validation.
6. **Performance and release.** Profile the same scene at the same stock
   console clocks with warm caches. Compare per-thread CPU, GPU time, frame
   percentiles and stutter counts, not only FPS or PC/emulator timing. Native
   deko3d is the release default only after visual parity and long-run checks.

## Optimization applicability

| Technique | Decision and correctness condition |
| --- | --- |
| Hook-safe direct calls and LTO | Already present; extend protected hook inputs and verify the linked symbols. |
| Register locals / loop locals / dead saves | Require MKvDCU split-function and caller-register audit. Earlier local-register attempts broke this title. Do not enable another game's list. |
| PGO / function ordering | Collect a new profile of this renderer on this game. Keep `_start` at address zero and reject mismatched profiles. |
| State and constant deltas | Compare exact bytes, emit changed blocks only, invalidate memo state after utility draws, clears and overlays. Include every consumed field in keys. |
| Draw-chain reuse and wake batching | Preserve ordering, flush immediately for waiters/callbacks, compare reconstructed snapshots against full snapshots. |
| Persistent vertex/index/texture data | Keep data alive through its last GPU fence. Start with exact hashes. Store cards require all writers, including guest, native, I/O and GPU writeback, to mark dirtiness. |
| Sampled hashes | Not a correctness proof for arbitrary streaming writes. Leave off until this game's invalidation and differential guards establish safety. |
| NEON upload/compare/byteswap | Compare results against scalar references and inspect AArch64 code. Avoid new atomics/clock reads per draw. |
| Constant pushes | One UBO slice per stage/work, full first upload, ordered changed vec4 pushes. Never overwrite CPU memory still consumed by earlier draws. |
| Descriptors | Ordered push-data writes; fence-delayed slot reuse. Cache handles with surface/resource epochs. |
| Command memory | Keep at least 256 bytes of slack per chunk. Bind one VBO per call to avoid deko3d 0.5's under-reservation bug. Re-add work-slot memory after its fence. |
| Images | Apply the deko3d 0.5 tile-height correction. Pool allocations to limit NvMap handles. Validate compressed format row/layer units. |
| Barriers | Begin conservatively, then remove only barriers without hazards. GPU timestamp scale must be calibrated; documented factor is 1.627. |
| ZCULL and compression | Enable with correct depth invalidation after uploads, copies or image replacement. Compressed depth copies use 3D; do not assume copy-engine safety. |
| Clear/resolve elision | Require actual MKvDCU attachment/alias and write/read tracking. NFSMW's car-shadow, sky and front-buffer rules are game-specific. |
| Early depth / alpha specialization | Variant only when shader discards, depth outputs and stencil/depth writes permit it. SA2 found an alpha-specialization regression: do not blindly enable it. |
| Native quads/fans | Preserve topology, restart, provoking-vertex and flat-interpolator semantics. |
| Internal resolution / MSAA / buffering | User options after parity; verify display-size swapchain recreation and distinguish quality changes from optimizations. |
| Core masks / late input | Each running thread applies its own mask at a wait. Hook the actual controller-read path and measure latency. No cross-thread affinity calls that can deadlock. |
| Audio/video natives | Runtime fixes already carried by the SDK stay. NFSMW game decoder/audio addresses cannot be copied into MKvDCU. |

## Earlier feasibility results (before native integration)

The portable Windows build chain is working entirely without Docker or WSL.
New tools and installation caches are under `E:/tools`. The standalone
`dist/deko3d-probe/mkvsdcu-deko3d-probe.nro` links deko3d only, has entry point
zero and validated NRO/ASET headers. It is **a platform test, not MKvDCU**;
its hardware behavior has not been tested.

The reconstructed-package census contains 19,340 unique shader containers
(1,703 vertex and 17,637 pixel), with no decompression failures. All 19,340
translate to SPIR-V after adding the required COLOR2 interface to both stages.
The 68 executable shaders pass both independent CLI and resident DKSH paths,
with identical output and zero warnings. The full resident DKSH audit passes
**19,340/19,340, zero failures, zero warnings**, and every compile was repeated
with identical bytes. The compiled blobs total 106,319,616 bytes. The original
30-minute worker timeout left 75 missing results; a hash-verified resume
compiled all 75. Both the timeout report and final report are retained under
`research/mkvsdcu-deko3d/all-shaders-dksh/`. All tests cover default
specialization values; observed gameplay variants remain pending.

The D3D catalog verifies 28 static body hashes across a 1,053-function catalog,
including ring allocation, segment submission and fence completion paths.
MKvDCU's BlockOnFence tests its no-kick guard in r7; the complete signature is
still marked provisional. Matching the XDK version does not justify copying
another title's typed hook signature without checking its code.
The optional PC trace passed 405 synthetic calls, full context/return-value
preservation, concurrent capture and invalid-pointer checks; its shader hash
was independently verified. These tests do not establish runtime call coverage.
See [the trace notes](../deko3d-tools/D3D_TRACE.md) and
[tool provenance/workflow](../deko3d-tools/README.md).

The native integration is now implemented as an opt-in target with a separate
shader pipeline. See `targets/mkvsdcu-nx/DEKO3D.md` and the local build/shader
verification reports for current results. Existing `dist/mkvsdcu.nro` remains
the prior Vulkan build. Console visual and performance acceptance gates remain
open; subsequent hardware testing is recorded below.

## Native artifact verification (2026-10-21)

The separate native target now builds successfully on Windows without Docker
or WSL. The final 73,246,917-byte NRO passes entry-point, NRO/ASET, deko3d
symbol and strong-hook checks. All 11 shader/draw hooks are protected from
direct-call rewriting; the staged generated source contains zero bypasses.
The linked native build has no Vulkan/NVK driver entry points.

The runtime ABI v4 corpus passes all 19,340 SPIR-V and GLSL conversions and
two identical DKSH compilations per shader, with zero failures or diagnostics.
The production reader verifies every original container, all 15,474
deduplicated cache entries and corrupted-cache rejection. Scratch metadata
sets 218,624 bytes per warp against a measured maximum of 153,088.

The deliverable is `dist/mkvsdcu-deko3d.zip`; its installation guide and
hash/size manifests are included. Hardware boot, visual parity and measured
performance were untested at initial packaging.

### First console crash and audio correction

The user subsequently supplied FTP access and an Atmosphere/rex crash report.
The first NRO initialized deko3d, opened the full native library/cache and
entered game startup. The captured access violation is a null driver in
`AudioSystem::SubmitFrame` at offset `0x12ad50`; it is not a GPU exception.
The guest callback can unregister/clear its handle before submitting its last
frame. In release builds the kernel's handle assertion disappeared, converting
zero to client slot zero and dereferencing its retired driver.

`audio-client-lifetime.patch` supplies release-build handle/slot checks,
idempotent retirement and publication-before-wake ordering. The regression
compiles the actual production function bodies against a fake audio device:
the original SDK faults with an access violation; the patched SDK passes
invalid/zero handles, stale slots, valid delivery, duplicate retirement and
concurrent submission/retirement. Native rebuild and console retry follow;
this evidence does not establish gameplay parity or performance.

### Gameplay capture and rendering correction

The user confirmed the audio-corrected build enters fights with improved
performance, but incomplete graphics. A captured 190-draw fight frame exposed
55 skipped utility draws. Seven small live programs were retrieved over FTP,
translated from their exact patched instruction bytes, and merged into the full
static corpus. The depth-copy pair retains captured baked constants and varyings;
its vertex fetches use live GPU constants. Production matching resolves all seven
captures exactly. All 19,347 shaders are covered by 15,481 verified native cache
entries; repeated compilation is deterministic and scratch limits remain valid.

The draw frontend now permits memory export without a pixel shader (all 64
static export programs verified). Invalid color-resolve destination formats use
the source surface format, matching the PC reference across 1,024 combinations.
Native Windows rebuild, verified deployment and console visual retry follow.

### Depth-clear surface aliases

The utility/resolve update still rendered only UI on the console. Captures show
the XDK clearing through 520-pixel 4x MSAA depth views and drawing through
1040-pixel 2x MSAA views at the same EDRAM base. They have identical physical
sample pitch; the previous host-image key separated their contents. This is
the same clear-alias mechanism described in the pinned SA2 `docs/sa2-notes.md`.

The native backend now recognizes the exact captured XDK clear programs and
replays unconditional uniform depth/stencil clears across compatible surface
views in sample coordinates. It preserves partial scissors, includes MSAA in
depth-image/pass identity, and invalidates clear records on nonuniform writes.
49,473 cases check pitch and physical sample coverage against SDK conventions;
a test of the production recognition/replay functions uses captured microcode
and covers padded images, stale records, partial geometry and alpha/stencil
discard. Console verification remains pending. Nonuniform surface transfers
are not established by this clear-specific correction.

The first retry stalled after audio retirement before any clear replay. The
same NRO subsequently booted with the normal TOML; startup remains intermittent,
and the collected crash report was the older, already fixed null-driver crash.
Log `mkvsdcu_009.log` then established why the alias correction had no effect:
the clear geometry was 520x624 (or 520x312), but the scissor stayed at the padded
520x720 allocation. Requiring full scissor coverage rejected every scene clear.
Recognition now intersects integer-aligned clear geometry with the scissor and
image bounds. The production regression uses that observed padded scissor,
checks partial/scissored coverage and both 2x/1x target aliases, and rejects
fractional edges that could cover only some samples. This revision still needs
console verification. Temporary stack sampling has been removed from the SD.
