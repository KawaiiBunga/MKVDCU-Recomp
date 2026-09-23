[CmdletBinding()]
param(
  [Parameter(Mandatory, Position=0)][string]$SourceDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Target = 'mkvsdcu'
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Not a directory: $SourceDirectory" }

$xex = Join-Path $source 'default.xex'
if (-not (Test-Path -LiteralPath $xex -PathType Leaf)) { throw 'No root-level default.xex found. The current manifest expects it at the game directory root.' }
$stream = [System.IO.File]::OpenRead($xex)
try {
  $magic = New-Object byte[] 4
  if ($stream.Read($magic, 0, 4) -ne 4 -or [Text.Encoding]::ASCII.GetString($magic) -ne 'XEX2') {
    throw "default.xex does not begin with XEX2: $xex"
  }
} finally {
  $stream.Dispose()
}

$work = Join-Path $root "user-game-files\work\$Target"
$sourceFull = [System.IO.Path]::GetFullPath($source).TrimEnd('\')
$workFull = [System.IO.Path]::GetFullPath($work).TrimEnd('\')
if ($sourceFull.Equals($workFull, [StringComparison]::OrdinalIgnoreCase) -or
    $sourceFull.StartsWith($workFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $workFull.StartsWith($sourceFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Source directory must be separate from the working copy.'
}
$reportDir = Join-Path $root "targets\$Target\reports"
if ((Test-Path -LiteralPath $work -PathType Container) -and
    @(Get-ChildItem -LiteralPath $work -Force | Select-Object -First 1).Count -gt 0) {
  throw "Working copy is already populated: $work. Use a fresh workspace or clear it deliberately before staging another dump."
}
New-Item -ItemType Directory -Force -Path $work,$reportDir | Out-Null
Write-Host "Copying supplied files to ignored working tree: $work"
$sourceItems = Get-ChildItem -LiteralPath $source -Force
foreach ($item in $sourceItems) {
  Copy-Item -LiteralPath $item.FullName -Destination $work -Recurse -Force
}
$workingXex = Join-Path $work 'default.xex'
if (-not (Test-Path -LiteralPath $workingXex -PathType Leaf)) {
  throw "Working-copy XEX was not created: $workingXex"
}
$hashes = Get-ChildItem -LiteralPath $work -Recurse -File | ForEach-Object {
  $h = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
  [PSCustomObject]@{ Path = $_.FullName.Substring($work.Length).TrimStart('\'); Bytes = $_.Length; SHA256 = $h.Hash }
}
$report = [PSCustomObject]@{
  target = $Target; created_utc = (Get-Date).ToUniversalTime().ToString('o');
  source_read_only_by_script = $source; working_copy = $work; primary_xex = $workingXex;
  primary_xex_magic = 'XEX2'; files = $hashes
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reportDir 'bring-up.json') -Encoding utf8
Write-Host "[OK] Validated XEX2 signature and wrote hashes: $reportDir\bring-up.json"
Write-Warning 'No codegen was run: target-specific executable revision, XEXP, modules, and configuration must be confirmed from the supplied dump first.'
