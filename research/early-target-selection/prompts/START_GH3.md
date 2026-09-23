# Follow-up prompt: GH3

> Archived planning prompt. The current MKVDCU-Recomp scripts do not stage GH3.

I have supplied a legally obtained, extracted Xbox 360 Guitar Hero III dump and ran `./setup-target.ps1 gh3 "<path>"`. Read `targets/gh3/reports/bring-up.json` and `docs/TARGET_INPUTS.md`. Validate the exact XEX metadata, region/media ID/version, title-update relation, modules, and asset layout without modifying `user-game-files/original`. Initialize the official ReXGlue project against the copied working input, run host codegen, and resolve the first errors systematically toward an offline host boot. Keep controller input separate from `GuitarState`; do not attempt a Switch build, DLC, network, or USB-guitar driver until the host runtime is understood.
