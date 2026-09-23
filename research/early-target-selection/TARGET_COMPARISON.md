# MK9 / GH3 / MK vs. DC Universe target comparison

> Historical pre-selection snapshot from 20 September 2026. MK vs. DC Universe was later chosen and reached playable PC gameplay. See [current status](../../docs/STATUS.md).

This is a feasibility comparison, not a target recommendation. No MK9, GH3, or MK vs. DC Universe executable, assets, title update, or generated code was acquired or analyzed.

| Factor | Mortal Kombat (2011) / Komplete Edition, Xbox 360 | Guitar Hero III: Legends of Rock, Xbox 360 |
|---|---|---|
| Exact candidate researched | Retail Xbox 360 MK9 metadata reported a `default.xex`, title ID `575207FD`, PE name `MK9Game-Retail.exe`, base version `0.0.0.4`; Komplete Edition is a later Xbox 360 bundle/release and must be treated as a distinct executable. | Retail Xbox 360 GH3 only at this stage; exact region, disc revision, title ID/media ID, and update revision are unconfirmed and must come from the supplied dump. |
| Engine/middleware | Heavily modified Unreal Engine 3, with a custom 2D fighting layer. | Neversoft rhythm-game technology; exact Xbox 360 middleware/module set must be inventory-verified. |
| XEX/modules | One title XEX is known; companion XEX/DLL modules have not been established from authoritative public data. | Primary XEX expected by any ReXGlue flow; companions unknown. Do not assume a single-XEX build. |
| Graphics | High-detail character/arena rendering, postprocessing, skeletal animation, particles/blood, UI/cinematics. Likely a difficult Xenos→modern GPU replacement. | Rhythm-game scenes, character/venue animation, UI and video effects. Likely lower scene complexity, but must reproduce timing-sensitive rendering and asset/shader behavior. |
| Audio | Music, voice, effects, streamed/cinematic audio; audio correctness matters but gameplay is not driven by per-note calibration. | Central technical risk: multichannel stems, stream scheduling, A/V latency, song/chart synchronization, and mixing. |
| Input | Standard fight controls; controller mapping is straightforward. | Standard controller plus five-fret guitars, strum, whammy, tilt, dongles/USB HID; the generic `GuitarState` is prepared precisely for this risk. |
| Filesystem | UE3 packages/cooked content and DLC/Komplete assets; need exact mounted paths from a real dump. | Large song/audio/chart/venue libraries; DLC/custom content and path behavior may be important. |
| Networking | Original online services/features should be scoped out for initial offline boot. | Online services/leaderboards should be scoped out initially; local controller/audio latency takes priority. |
| Known public recomp work | No public ReXGlue project was located in this research pass. | Community posts advertise a contemporary GH3 ReXGlue effort, but a canonical public source repository and its supported exact dump were not confirmed; do not rely on it as an input contract. |
| Existing community knowledge | Mature MK9 modding and a Windows release may help asset and behavior research, but it is not a source-level port. | Extensive Guitar Hero modding/chart/tooling knowledge and controller communities are useful later, especially for assets and guitars. |
| Switch load | GPU/RAM pressure is plausibly high, especially for UE3 scenes, shaders, and streaming. Measure rather than estimate after host boot. | CPU/GPU likely less scene-heavy, but tight audio scheduling and storage bandwidth may dominate perceived quality. |
| Likely missing Xbox APIs | Kernel/process/thread/VM, XInput-style input, filesystem, XMA/audio, Xenos graphics, services. | The same baseline APIs plus peripheral/HID behavior and potentially music licensing/content storage edge cases. |

## Mortal Kombat vs. DC Universe — dedicated assessment

- **Exact retail target:** Xbox 360 retail only, not the public prototype. Public title-ID listings identify it as `4D5707E9` (Midway `MW-2025`). A separately documented 2008 prototype reports `/default.xex` and original PE name `MK8Game-Retail.exe`; those details are useful identification hints, not permission to substitute the prototype or assume a retail hash.
- **Engine / executable complexity:** Midway’s 2008 UE3-based fighter, with PhysX credited. The engine was already heavily modified, and historical UE3 tooling reports a non-standard Xbox 360 asset layout. Expect the same XEX/PPC, kernel, VM, filesystem, thread, Xenos graphics, and audio import classes as other 360 targets; companion modules must be discovered from the supplied retail dump.
- **Graphics / Switch load:** It predates MK9 and has a narrower fighter format, so it is a reasonable hypothesis that scene and memory pressure could be lower than MK9. That is not a performance conclusion: modified UE3 rendering, post-processing, skeletal animation, effects, and shader translation remain serious Switch work.
- **Audio / input / filesystem:** Standard fight controls make initial input materially simpler than GH3; no guitar/dongle layer is needed. Audio is still important but does not have GH3’s stem synchronization and latency contract. Preserve all shipped UE3 content so package/mount behavior can be learned without assumptions.
- **Networking:** Limit the first host boot to offline modes. Online services and DLC are not an initial goal.
- **Known ReXGlue work:** No public ReXGlue/XenonRecomp project for the retail Xbox 360 version was found in this research pass.
- **Practical implication:** Of these three, MK vs. DCU looks like the most attractive *first investigation candidate* if your priority is a conventional controller fighter and you accept UE3/Xenos porting risk. It should still earn that position by passing a host codegen/import inventory against one exact retail dump before any Switch commitment.

## Practical reading

MK9 likely concentrates risk in UE3/Xenos rendering, memory, asset streaming, and broad engine imports. GH3 may be more approachable graphically, but successful play depends on audio latency and instrument input that cannot be deferred indefinitely. MK vs. DCU may be a better-scoped controller-first investigation than either, but its modified UE3/Xenos runtime is still unported. None has an established, drop-in ReXGlue-to-Switch path. A sensible first milestone is an exact-version host codegen/import report with no network/DLC ambitions; Switch work should follow only after the host runtime boundary is understood.

Sources: [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), [MK9 engine/release overview](https://en.wikipedia.org/wiki/Mortal_Kombat_(2011_video_game)), [MK9 Xbox 360 metadata report](https://forum.bazicenter.com/threads/mortal-kombat-xbox-360.30896/page-24), [MK vs. DCU title-ID listing](https://consolemods.org/wiki/Xbox_360%3AList_of_Every_Xbox_360_Title_ID), [MK vs. DCU prototype metadata](https://hiddenpalace.org/Mortal_Kombat_vs._DC_Universe_%28Oct_30%2C_2008_prototype%29), and public project references in [Switch research](../../docs/switch/RESEARCH.md).
