# Xbox 360 recompilation → Switch research

> Research snapshot from 20 September 2026. MKVDCU-Recomp now has a playable **PC** build; this document does not establish a playable Switch build. See [current status](../STATUS.md).

Research snapshot: 2026-09-20. Reference clones are intentionally ignored by Git and contain open-source code only. Their pinned checked-out revisions are recorded here so a later refresh is comparable.

| Project | Revision inspected | What it establishes | Reuse verdict |
|---|---:|---|---|
| [rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk) | `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` (`v0.10.0`) | Current generic Xbox 360 XEX analysis/codegen/runtime SDK. Host presets are Windows/Linux/macOS; no supported Switch preset was found. | Codegen concepts and host tooling: yes. Switch runtime: not yet generic/upstream. |
| [mchughalex/skate3recomp](https://github.com/mchughalex/skate3recomp) | `f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd` | Active native Skate 3 recomp for desktop; its SDK submodule is a Skate-specific fork. It uses a native renderer rather than a transferable Xenos emulation layer. | ReXGlue integration patterns only; renderer and game overrides are Skate-specific. |
| [NaGaa95/Skate3Recomp-NX](https://github.com/NaGaa95/Skate3Recomp-NX) | `56bb525cb26f9c697e337326723f424fa4b294c2` | Switch fork associated with NaGaa95; adds a devkitA64 toolchain and asset packaging around its own Skate runtime. | Toolchain/package design is reusable; no generic runtime port is demonstrated. |
| [hedge-dev/UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) | `cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c` | Upstream XenonRecomp-based Sonic Unleashed project. | Useful contrast for a mature target-specific runtime. |
| [NaGaa95/UnleashedRecomp-NX](https://github.com/NaGaa95/UnleashedRecomp-NX) | `84932ed066fecf2670960f4296e2dc8404e2b975` | Switch fork with libnx OS/HID adaptations, NRO packaging, a devkitA64 CMake toolchain, and an NVK-facing build. | The clearest Switch porting reference, but target shader/input/runtime work must not be copied blindly. |
| [sonicnext-dev/MarathonRecomp](https://github.com/sonicnext-dev/MarathonRecomp) | `04431570d1ad56eaf3dda9ddd9f0efc628c455f5` | Upstream Sonic 2006 project. | Baseline for a second independent Switch fork. |
| [NaGaa95/MarathonRecomp-NX](https://github.com/NaGaa95/MarathonRecomp-NX) | `eb59c3f5eb9efe4197d703f05fb86aa6cf82cb6f` | Parallel Switch port with nearly the same platform layout: devkitA64, libnx, NRO, NVK stubs, HID, runtime/process/logging shims. | Corroborates the generic boundary identified below. |

## Other NaGaa95 Switch work

[gtasa_nx](https://github.com/NaGaa95/gtasa_nx) and [NetherSX2_nx](https://github.com/NaGaa95/NetherSX2_nx) are not Xbox 360 ReXGlue recompilations, so they were not cloned as primary code references. They are nevertheless useful operational evidence: NaGaa95’s Switch projects use standard devkitPro portlibs, package homebrew for title-override use when memory/syscalls require it, and expose device/configuration behavior through a project-owned layer. Their Android/PS2-emulator wrappers do not establish a reusable PowerPC, Xenos, or ReXGlue runtime implementation and must not be treated as one.

## What the Switch forks actually add

Both NaGaa95 forks add a CMake cross-toolchain that selects `aarch64-none-elf-{gcc,g++}`, compiles for Cortex-A57/ARMv8-A with `-fPIE`, includes libnx and Switch portlibs, and links through `libnx/switch.specs`. Both package a stripped ELF plus NACP/icon through `elf2nro`. Those are reusable build mechanics, represented here by `switch/toolchain-test/` and the historical `research/switch-scaffold/cmake/switch-devkitA64.cmake`.

They also contain similar per-platform implementations for logging, process/runtime/user/version/registry behavior, HID input, and places where Windows-style APIs have no Switch counterpart. This is evidence that a port needs a deliberate OS ABI layer. It is **not** evidence that any one shim has the right semantics for MK9 or GH3.

Unleashed’s Switch build explicitly requires game-specific `default.xex`, `default.xexp`, `shader.ar`, generated PPC source, generated shader cache, a compatible NVK/Mesa static library, and special shader-generation host tooling. Marathon has analogous renderer and OS substitutions. These are target build inputs, not reusable SDK dependencies.

## Component-by-component disposition

| Area | Evidence | Status for this workspace |
|---|---|---|
| devkitA64 / ARM64 | All three NX references use devkitA64 cross compilation. | Reusable, installed and smoke-tested. |
| libnx startup/NRO | References use libnx spec linking and NRO packaging. | Reusable, smoke-tested. |
| CMake cross toolchain | Fork toolchains establish ordinary compiler/sysroot discovery. | A clean generic template is provided; it does not claim ReXGlue builds with it. |
| Filesystem and logging | Forks add local policy/compatibility shims. | Interface boundary only; path conventions must be designed after target bring-up. |
| Threads, memory, VM, sync | Xbox kernel behavior is an application/runtime correctness issue; fork code is coupled to its source engine. | Unfinished generic service; must be validated under ReXGlue’s runtime contract. |
| SDL / audio | ReXGlue uses host-oriented UI/runtime dependencies; NaGaa95 ports replace/avoid pieces per project. | No generic implementation copied. `switch-sdl2` is a possible dependency, not installed or assumed. |
| Controller / USB | libnx HID is present in NX forks, but their mappings are game-specific. | Generic `GuitarState` boundary added; no device driver pretended. |
| Vulkan / NVK / deko3d | The NX forks target NVK/Mesa and carry ICD/EGL/Vulkan stubs and application shaders. They do not establish a universal D3D12/Xenos→NVK adapter. | Major blocker. No Mesa/NVK binary or fork code imported. |
| Shader translation | Unleashed needs target shader archive extraction and XenosRecomp/DXC workarounds. | Game-specific; no cache or generated shader code is reusable. |
| PPC generated code | ReXGlue/XenonRecomp output depends on executable layout, function boundaries, imports, endianness, alignment, and ABI. | Never shared between games; ignored by default. |

## Upstream candidates

The appropriate upstream candidates are a documented devkitA64 preset/toolchain, a platform-neutral runtime abstraction with tested memory/thread/synchronization semantics, and optional NRO packaging hooks. Per-game OS function bodies, shader/renderer replacements, executable addresses, codegen configs, title-update assumptions, PPC sources, and compatibility patches do **not** belong upstream.

## Current integration conclusion

The official ReXGlue SDK is host-oriented at this snapshot. A normal libnx application builds here, but a ReXGlue application cannot truthfully be claimed to build until its host dependencies and runtime ABI are ported and validated on ARM64/libnx. Start with a host recompilation for the selected exact executable; then define and test one platform service at a time before attempting the full Switch link.
