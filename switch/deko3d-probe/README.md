# Native deko3d platform probe

This is a standalone platform test, **not the MKvDCU game port**. It verifies
the Windows cross toolchain, patched uam, DKSH loading, deko3d device/queue,
render-target setup and native presentation without a Vulkan driver. It
contains no game shader or game data.

Build from PowerShell at the repository root:

```powershell
./scripts/build-deko3d-probe.ps1 -Python '<existing-python.exe>'
```

Tools default to `E:/tools`. The script sets temporary storage there and
checks the linked ELF's `_start` is zero, deko3d submission is present, and the
NRO and homebrew asset headers are valid. Output:
`dist/deko3d-probe/mkvsdcu-deko3d-probe.nro`, with `verification.json` beside it.
The existing `dist/mkvsdcu.nro` is not replaced.

Run the probe through Homebrew Menu. Expected picture: red top-left, green
top-right, blue bottom-left and white bottom-right. Press Plus to exit. The
probe writes `sdmc:/mkvsdcu-deko3d-probe.log`, reporting every 600 frames and
at clean shutdown. Leave it running for at least ten minutes during platform
bring-up. Its debug deko3d callback logs errors before terminating.

Command lists are immutable while in use, command memory leaves 256 bytes
of slack, viewports have positive dimensions, and shader code leaves
`DK_SHADER_CODE_UNUSABLE_SIZE` unused. The game renderer will additionally
need per-slot fences and upload lifetimes, image tile corrections, descriptor
updates, texture/depth/stencil/resolve tests, menu integration and frame pacing.

The native API setup was cross-checked against the
[switchbrew deko3d example](https://github.com/switchbrew/switch-examples/tree/master/graphics/deko3d/deko_basic)
and the pinned SA2/NFSMW notes. A cross build does not establish a successful
hardware run; `hardware_tested` stays false until one is actually recorded.
