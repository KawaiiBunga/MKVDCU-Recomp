# Follow-up prompt: MK9

> Archived planning prompt. The current MKVDCU-Recomp scripts do not stage MK9.

I have supplied a legally obtained, extracted Xbox 360 Mortal Kombat (2011) / Komplete Edition dump and ran `./setup-target.ps1 mk9 "<path>"`. Read `targets/mk9/reports/bring-up.json` and `docs/TARGET_INPUTS.md`. Validate the exact XEX metadata, edition/region/media ID/version, title-update relation, modules, and complete asset layout without modifying `user-game-files/original`. Initialize the official ReXGlue project against the copied working input, run host codegen, and resolve the first errors systematically toward an offline host boot. Do not apply title updates speculatively, carry UE3/game-specific patches into the generic template, or attempt Switch rendering until the host runtime is understood.
