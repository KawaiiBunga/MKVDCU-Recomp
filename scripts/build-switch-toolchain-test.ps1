[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$devkit = if (Test-Path 'C:\devkitPro') { 'C:\devkitPro' } elseif ($env:DEVKITPRO -and (Test-Path $env:DEVKITPRO)) { $env:DEVKITPRO } else { throw 'devkitPro was not found.' }
$env:DEVKITPRO = $devkit.Replace('\', '/')
$env:DEVKITA64 = Join-Path $devkit 'devkitA64'
$env:LIBNX = Join-Path $devkit 'libnx'
$env:PORTLIBS = Join-Path $devkit 'portlibs/switch'
$env:Path = "$(Join-Path $env:DEVKITA64 'bin');$env:Path"

Push-Location (Join-Path $root 'switch\toolchain-test')
try {
  & (Join-Path $devkit 'msys2\usr\bin\make.exe')
  if ($LASTEXITCODE -ne 0) { throw "Switch toolchain test build failed ($LASTEXITCODE)." }
  New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null
  Copy-Item '.\mkvdcu-switch-toolchain-test.nro' (Join-Path $root 'dist\mkvdcu-switch-toolchain-test.nro') -Force
  Write-Host '[OK] dist/mkvdcu-switch-toolchain-test.nro built (toolchain only).'
} finally {
  Pop-Location
}
