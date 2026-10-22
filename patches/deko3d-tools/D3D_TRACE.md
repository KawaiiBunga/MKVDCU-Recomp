# Original-renderer D3D trace

This diagnostic is a prerequisite for installing MKvDCU's native rendering
hooks. It does not replace a renderer or enable deko3d in the game.

`targets/mkvsdcu-nx/config/deko3d-d3d-map.json` records 28 static function
identifications for the user's XDK 2.0.7645 executable, including body hashes,
signature evidence, and the fact that runtime confirmation is pending.
`scripts/audit-switch-d3d.py` verifies those hashes and catalogs 1,053 functions
and their video-kernel calls. Capstone cannot decode every Xbox VMX128 word;
the disassembly retains unknown words and continues through each function.

The PC target's `MKVSDCU_D3D_TRACE` option is **off by default**. When enabled,
15 strong hooks capture arguments before calling the original function exactly
once. Creation, shader registration, draw variants, inline draws, clears,
resolves and Swap are included. No typed argument marshaling or replacement
device is introduced. The trace records:

* guest function, caller, stack pointer, thread, QPC and its frequency;
* raw r3-r10 and the bits of f1, without assuming unverified signatures;
* checked snapshots of six known bound-object fields;
* SHA-256 of the concatenated virtual/physical container at shader registration,
  using checked reads and bounded sizes. An unreadable container is `null`.

Enable it in a **separate PC build** of the existing host with
`-DMKVSDCU_D3D_TRACE=ON`. Launch that executable with a fresh capture path:

```powershell
$env:MKVSDCU_D3D_TRACE_PATH = 'D:/Code Projects/MKvDCU-Recomp/research/mkvsdcu-deko3d/game-d3d-trace.jsonl'
# Launch the trace-enabled PC executable with the normal game arguments.
```

The capture uses exclusive creation, stops after 50,000 events, flushes every
256 events, and closes on normal shutdown. An existing path is never replaced.
This synchronous diagnostic has overhead and is unsuitable for benchmarking.
It captures an early call window, not every gameplay state or inline instruction.
The original renderer was run from an isolated rebuilt diagnostic executable.
Its 50,000-event capture observed 14 of the mapped functions, with zero failed
device snapshots or shader-identity reads. A second hidden, muted 90-second run
exited normally. The original executable and generated inputs were preserved.

Summarize an actual game capture with:

```powershell
python scripts/summarize-switch-d3d-trace.py `
  --trace research/mkvsdcu-deko3d/game-d3d-trace.jsonl `
  --containers-manifest research/mkvsdcu-deko3d/shader-audit-complete/manifest.json `
  --output research/mkvsdcu-deko3d/game-d3d-trace-report.json
```

The report distinguishes object addresses from known container identities,
compares runtime registration hashes with the static census, and reports
failed reads. Its executable hash is an expected identity, not a runtime
verification. An absent call is not evidence that the game never uses it.

## Host verification

`tools/deko3d-d3d-trace-smoke` builds the real hooks against synthetic memory
and stub originals, without the game or the SDK runtime libraries. Native
llvm-mingw builds go under `E:/tools/build/d3d-trace-smoke`. The test passed
405 original calls, including concurrent callers and invalid device/shader
pointers. It compares the entire PPC context before and after each hook,
including return-value changes made by the stub original. Its container SHA-256
was independently checked against Python hashlib. These are diagnostic tests,
not evidence of gameplay or visual correctness.

The 15 diagnostic addresses are protected by the existing direct-call staging
scanner. The PC generated sources contain 43 alias calls to them and zero
direct original-function calls that would bypass these hooks.

## Runtime capture and binary dump follow-up

Local captures are in `research/mkvsdcu-deko3d/pc-trace-run-20261020-223505`
and `pc-trace-run-20261020-224534`. Five VS registration identities were absent
from the static census. Registration may precede the caller's physical copy;
the native adapter therefore retries unknown objects at binding/first draw
before FlushState patches them. These five hashes were not added as shaders.

The optional dump initially opened Windows files in text mode (`wx`), expanding
LF bytes to CRLF. It now uses binary exclusive creation (`wbx`). The corrected
405-call smoke run emitted exactly 84 fixture bytes, with independently matching
SHA-256. Earlier dumps must not be accepted by filename alone: verify their
bytes against the recorded identity. Capture evidence covers an early call
window, not fight rendering, tiling or complete runtime shader coverage.
