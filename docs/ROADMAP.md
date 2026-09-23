# Roadmap

## Now: first public release

- Publish `v0.2.0` with `scripts/release.ps1 -Publish` and run the full first-time setup on a clean Windows PC (no Visual Studio, no LLVM) through **Play**.
- Code-sign the launcher (SignPath Foundation or Azure Trusted Signing; see [Releasing](RELEASING.md#code-signing)).
- Test an update from `v0.2.0` to the next release, including the incremental rebuild.

## PC stability

- Play more modes, characters, stages and long sessions. Every `Call to invalid or unregistered function` report gets its address added to the function list for the supported XEX.
- Test other GPUs (AMD, Intel), controllers and display setups.
- Pause the game when the F1 menu is open, if a safe pause path is found. Today only input is blocked.

## Performance

Measured leads and open ideas are in [Performance](PERFORMANCE.md). The next steps:

- Replace the game's spin-waits (`Sleep(0)` loops on the main thread, ring-buffer polling on the render thread) with real waits. This saves power, not frame rate.
- Map setjmp/longjmp so the stronger codegen options (`non_volatile_as_local`, `skip_lr`) can be tried.
- A real high-frame-rate mode needs the game's update step decoupled from the 60 Hz vblank. Frame interpolation isn't possible from the presenter.

## Mods

- Seed `mods/index.json` with the first mods and document common file formats as modders work them out.
- A shareable mod-pack or "profile" export if players ask for it.

## Switch

The libnx toolchain test is separate from the game. A Switch port needs the ReXGlue runtime, memory, threading, filesystem, input, audio and a Xenos graphics backend on Switch. The menu's settings catalog is designed to be reused there. See [Switch research](switch/RESEARCH.md).
