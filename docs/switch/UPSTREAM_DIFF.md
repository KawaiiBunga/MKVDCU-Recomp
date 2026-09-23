# Upstream → Switch diff classification

> Historical Switch design review. The active game target is PC; see [current status](../STATUS.md).

This classification is based on file-level comparison of the cloned upstream and NX trees on 2026-09-20, not on filename similarity alone. Upstream repositories do not share the same commit graph with every Switch fork, so this is a maintained design review rather than a mechanically exact merge proposal.

## GENERIC / REUSABLE

- `toolchains/switch-devkitA64.cmake`: detect devkitPro, select the bare-metal AArch64 compiler/binutils, select static-library `try_compile`, add ARMv8-A/Cortex-A57/PIE flags, and search libnx/portlibs.
- NRO workflow: produce a normal ELF, generate NACP metadata, strip a packaging copy, and invoke `elf2nro` with a project-owned icon.
- libnx startup conventions: use title-override rather than applet mode for memory-heavy applications; use `appletMainLoop`; clean up services before exit.
- A platform selection layer that makes each OS implementation explicit rather than compiling Windows calls on Switch.
- HID architecture: translate libnx pad state into a project-owned normal-controller abstraction. Keep USB/device transport separate from gameplay mappings.
- Build separation: native host analysis/codegen tools run on the host; generated PPC/game runtime is cross-compiled only after its dependencies exist for Switch.

## GAME-SPECIFIC

- Every `*_switch_tables.toml`, PPC config, generated `ppc/*.cpp`, function map, image base, code address, import shim, and title-update patch.
- Unleashed `shader.ar` extraction, XenosRecomp/DXC workarounds, shader cache object, generated SPIR-V headers, and all engine renderer hooks.
- Marathon’s SonicTeam API layer and individual EGL/NVK placeholders.
- Skate 3’s native scene renderer, game data path assumptions, game-specific ReXGlue fork, input/timing patches, and assets.
- NVK static-library location/build configuration and any workarounds for a given Mesa revision; these must be pinned and tested per target.
- Registry/process/user/version implementations where their behavior emulates a particular game’s expectations rather than a documented generic ReXGlue contract.

## UNKNOWN — investigate before reuse

- Whether official ReXGlue’s current runtime can be compiled as freestanding/libnx C++ and which SDL/UI/video dependencies must be replaced or made optional.
- ReXGlue’s required virtual address-space layout, allocation granularity, page protection, atomic wait behavior, and PPC alignment guarantees on Switch.
- Exact Vulkan loader/ICD expectations, a legally redistributable NVK build strategy, and whether a compatible presentation path can be owned by this project.
- The compatibility of generated PPC C++ with devkitA64 GCC’s ABI/codegen; confirm exception, TLS, `long double`, atomics, unaligned access, and endian paths with small generated samples.
- MK9 and GH3 executable variants, title-update relation, loadable modules, encrypted/decrypted state, and required data/shader files.

## Deliberate non-imports

No code from the Switch forks was copied into `research/switch-scaffold/`. Their licenses, submodules, and target-specific assumptions need individual review if a later port truly needs a concept. This workspace only derives independent, small build/interface structures and documents the evidence.
