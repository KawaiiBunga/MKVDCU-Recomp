# User-supplied target inputs

> Historical multi-target planning notes. The active repo now targets MK vs. DC Universe only; use [Getting started](../../docs/GETTING_STARTED.md) and `scripts/stage-game-data.ps1` for current commands.

## Rules shared by both targets

The primary executable (`default.xex`) is required for a ReXGlue project because its codegen configuration names an XEX/ELF input. The exact original executable matters: function locations, codegen output, title updates, hashes, and generated code are tied to it.

Provide an **extracted Xbox 360 game directory you legally dumped**, not content downloaded from the Internet. Keep it outside the repository if practical. `setup-target.ps1` never writes to its source directory; it makes a gitignored working copy and emits SHA-256 hashes. The workspace ignores all `user-game-files/`, target private/generated data, reports, and build output.

Expected workspace layout after setup:

```text
user-game-files/
  original/                  # optional manual read-only staging; never committed
    gh3/, mk9/, or mkvsdcu/
      default.xex
      ... game data exactly as dumped ...
  work/                      # created, gitignored copy used by tools
    gh3/, mk9/, or mkvsdcu/
targets/
  gh3/ or mk9/
    private/                 # future per-target configuration/input links; ignored
    generated/               # generated PPC source; ignored
    reports/bring-up.json    # generated hashes/metadata; ignored
```

## MK9 / Mortal Kombat Komplete Edition

### Required now

- The complete extracted Xbox 360 game directory for one exact release/region, including `default.xex`.
- The dump’s region, disc/product edition, title ID, media ID, executable version/base version, original PE name, and SHA-256 of `default.xex`. The setup report hashes files; use an XEX metadata tool later to confirm fields.
- All game asset directories and files shipped beside the executable. Do not filter “non-code” files: they establish mounting and future filesystem research.

### Conditional / currently unknown

- `default.xexp`/title update: **not yet established as required**. Supply only the title update actually used by your dump, along with its source package/version and matching media ID; do not mix regions or revisions.
- Other DLL/XEX modules: **unknown** until the dumped directory and XEX imports are inventoried.
- DLC/Komplete content: not needed for the first base-game boot. If the supplied executable expects bundled content, retain it in the dump; do not add unrelated DLC.
- Decrypted XEX: ReXGlue’s ability to consume the exact dump state must be verified when the input arrives. Do not pre-decrypt or modify the original merely because another project did.

## Guitar Hero III: Legends of Rock

### Required now

- The complete extracted Xbox 360 GH3 directory for one exact retail region/revision, including `default.xex`.
- Title ID, media ID, executable/base version, region, disc revision, and SHA-256 for `default.xex`.
- All shipped song/audio/chart/venue/UI asset directories. They are needed to discover mounts and to eventually validate audio timing; do not reduce the dump to only the XEX.

### Conditional / currently unknown

- `default.xexp`/title update: **unknown**. If your legal install uses one, retain the exact matching update separately and record its version/media ID. Do not assume an update is either mandatory or safe to apply.
- DLL/XEX modules, shader caches, and archives: **unknown** until inventory/import analysis.
- DLC: not needed for first boot; defer it. Later support depends on exact title-update/content contracts.
- Controller/guitar data: no game file is needed for the abstraction. Physical USB guitar/dongle support will need device identification and a separate consented hardware test later.

## Mortal Kombat vs. DC Universe

### Required now

- A complete extracted Xbox 360 retail game directory from one exact region/disc revision, including `default.xex`.
- Region, edition (standard/Kollector's/Greatest Hits if applicable), title ID, media ID, executable/base version, original PE name, and SHA-256 for `default.xex`.
- Every asset directory/file next to the executable. Public UE3 tooling discussion confirms this title has a modified UE3 data layout; do not reduce it to the executable or substitute assets from another platform/release.

### Conditional / currently unknown

- `default.xexp`/title update: **not established as required**. Preserve only the update actually paired with the supplied dump and record its version/media ID before anyone applies it.
- DLL/XEX modules, archives, shader caches, and load order: **unknown** until the extracted directory and XEX imports are inventoried.
- DLC/online content: out of scope for initial offline boot. Do not add it to the first test set.
- Decrypted XEX: do not modify the original. Determine what ReXGlue accepts after metadata/integrity inspection of the supplied exact dump.

## What the setup command verifies

`setup-target.ps1 <target> <directory>` requires a discovered `default.xex` with an `XEX2` signature, copies the supplied directory into its ignored work location, hashes every copied file, and writes `targets/<target>/reports/bring-up.json`. It deliberately does not run codegen yet, claim decryption, infer DLL requirements, or apply an XEXP.
