# Making mods

A mod replaces or adds game files. The player's game folder is never changed: the port places the mod's files over the game's files in memory when the game starts, so turning a mod off is instant and safe.

## Layout

```
my-mod.zip
├── mod.json
└── files/
    └── Movies/
        └── WB_Logo.wmv      replaces the game's Movies\WB_Logo.wmv
```

`files/` mirrors the game folder. Any file in it is read instead of the game's file with the same path (case does not matter), and new files are added. `default.xex` cannot be replaced; the game code is recompiled, not loaded from it.

`mod.json`:

```json
{
  "id": "hd-intro-logos",
  "name": "HD intro logos",
  "version": "1.0.0",
  "author": "you",
  "description": "Sharper studio logos before the title screen.",
  "homepage": "https://github.com/you/hd-intro-logos"
}
```

`id` is lower-case letters, digits, `.`, `_` or `-`, at most 64 characters. Installing a mod with an existing id replaces that mod.

## Load order

The Mods page lists enabled mods first; **▲** and **▼** change their order. When two mods contain the same file, the one higher in the list wins.

## Testing

1. Put your folder (with `mod.json` and `files/`) in `%LOCALAPPDATA%\MKVDCU-Recomp\mods\<id>`, or zip it and use **Add from file…**.
2. Enable it and press Play.
3. The game log (`user\logs\play-*.log`) shows `Mod loaded: … (N files)`. Start the game with `--log_level debug` to list each replaced file.

Files the game streams from disc keep their format: a replacement `.xxx` package or `.wmv` movie must be what the game expects on Xbox 360 (big-endian, Xbox 360 cooked content). The game's `Xbox360TOC.txt` does not need editing; this game reads file sizes from the files themselves.

## Getting listed in the launcher

The **Get mods** list comes from [`mods/index.json`](../mods/index.json) in this repository. To add yours, open a pull request with an entry:

```json
{
  "id": "hd-intro-logos",
  "name": "HD intro logos",
  "version": "1.0.0",
  "author": "you",
  "description": "Sharper studio logos before the title screen.",
  "homepage": "https://github.com/you/hd-intro-logos",
  "url": "https://github.com/you/hd-intro-logos/releases/download/v1.0.0/hd-intro-logos.zip",
  "sha256": "64 hex characters",
  "size": 12345678
}
```

`url` must be HTTPS and must not change after listing. The launcher refuses a download whose SHA-256 differs. PowerShell prints the hash and size: `Get-FileHash file.zip; (Get-Item file.zip).Length`. For a new version, update `version`, `url`, `sha256` and `size` together.

Mods must contain only your own work or content you have permission to share: no files copied from the game or other games.
