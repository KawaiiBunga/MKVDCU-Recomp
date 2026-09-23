[CmdletBinding()]
param(
    [string]$GameDataRoot = ''
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Join-Path $workspaceRoot 'targets\mkvsdcu\private\rexglue-host'
$buildDir = Join-Path $projectRoot 'out\build\win-amd64-release'
$exe = Join-Path $buildDir 'mkvsdcu.exe'
$plugin = Join-Path $buildDir 'rexgpu-xenos.dll'
if (-not $GameDataRoot) { $GameDataRoot = Join-Path $workspaceRoot 'user-game-files\work\mkvsdcu' }
$xex = Join-Path $GameDataRoot 'default.xex'
$userRoot = Join-Path $projectRoot 'runtime-user'
$cacheRoot = Join-Path $projectRoot 'runtime-cache'
$log = Join-Path $buildDir ("pc-play-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

foreach ($required in @($exe, $plugin, $xex)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file is missing: $required. Run scripts/build-pc.ps1 and see docs/PC_BUILD.md."
    }
}
$expectedXexHash = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'
$xexStream = [IO.File]::OpenRead($xex)
$sha256 = [Security.Cryptography.SHA256]::Create()
try { $actualXexHash = [BitConverter]::ToString($sha256.ComputeHash($xexStream)).Replace('-', '') }
finally { $xexStream.Dispose(); $sha256.Dispose() }
if ($actualXexHash -ne $expectedXexHash) {
    throw "Game XEX SHA-256 $actualXexHash does not match this build's $expectedXexHash."
}
New-Item -ItemType Directory -Force -Path $userRoot, $cacheRoot | Out-Null

$arguments = '--no-audio_mute --gpu_plugin xenos --game_data_root "{0}" --user_data_root "{1}" --cache_root "{2}" --log_level info --log_file "{3}"' -f $GameDataRoot, $userRoot, $cacheRoot, $log
$process = Start-Process -FilePath $exe -WorkingDirectory $buildDir -ArgumentList $arguments -PassThru
Write-Host "[OK] Started MKVDCU-Recomp PC (PID $($process.Id))."
Write-Host "Log: $log"
