[CmdletBinding()]
param([string]$Version = 'v0.1.0-preview')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$package = Join-Path $dist 'MKVDCU-Recomp-win-x64'
$zip = Join-Path $dist 'MKVDCU-Recomp-win-x64.zip'
$rootFull = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
$packageFull = [IO.Path]::GetFullPath($package)
$zipFull = [IO.Path]::GetFullPath($zip)
if (-not $packageFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
    -not $zipFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package outputs escaped the workspace.'
}
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Force -Path $package | Out-Null

& dotnet publish (Join-Path $root 'launcher\MKVDCU.Launcher\MKVDCU.Launcher.csproj') `
    -c Release -r win-x64 --self-contained true '-p:PublishSingleFile=true' `
    '-p:IncludeNativeLibrariesForSelfExtract=true' -o $package
if ($LASTEXITCODE -ne 0) { throw 'Launcher publish failed.' }
& dotnet publish (Join-Path $root 'launcher\MKVDCU.Updater\MKVDCU.Updater.csproj') `
    -c Release -r win-x64 --self-contained true '-p:PublishSingleFile=true' `
    '-p:IncludeNativeLibrariesForSelfExtract=true' -o $package
if ($LASTEXITCODE -ne 0) { throw 'Updater publish failed.' }

$files = @(
    'scripts\build-pc.ps1',
    'targets\mkvsdcu\private\rexglue-host\CMakeLists.txt',
    'targets\mkvsdcu\private\rexglue-host\CMakePresets.json',
    'targets\mkvsdcu\private\rexglue-host\mkvsdcu_manifest.toml',
    'targets\mkvsdcu\private\rexglue-host\generated\rexglue.cmake',
    'targets\mkvsdcu\private\rexglue-host\config\mkvsdcu_functions.toml',
    'targets\mkvsdcu\private\rexglue-host\src\main.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\mkvsdcu_app.h',
    'targets\mkvsdcu\private\rexglue-host\src\mkvsdcu_app.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\port_menu.h',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\port_menu.cpp'
)
foreach ($relative in $files) {
    $source = Join-Path $root $relative
    $target = Join-Path $package $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing package file: $source" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}
$releaseManifest = [PSCustomObject]@{
    schema = 1
    version = $Version
    platform = 'win-x64'
    supportedXexSha256 = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'
    sdkRevision = 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518'
}
$releaseManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'release-manifest.json') -Encoding utf8
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
Write-Host "[OK] Package: $zip"
Write-Host "[OK] SHA-256: $hash"
Write-Warning 'Package needs the pinned ReXGlue SDK, LLVM, CMake, Ninja, and the user-owned matching game folder to build the host.'
