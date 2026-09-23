[CmdletBinding()]
param([string]$Version = 'v0.1.0-preview')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$package = Join-Path $dist 'MKVDCU-Recomp-win-x64'
$published = Join-Path $dist '_published-launcher'
$updater = Join-Path $dist '_published-updater'
$builder = Join-Path $dist '_builder'
$builderZip = Join-Path $dist '_builder.zip'
$zip = Join-Path $dist 'MKVDCU-Recomp-win-x64.zip'
$standalone = Join-Path $dist 'MKVDCU-Recomp.exe'
$distFull = [IO.Path]::GetFullPath($dist).TrimEnd('\') + '\'
foreach ($path in @($package, $published, $updater, $builder, $builderZip, $zip, $standalone)) {
    if (-not [IO.Path]::GetFullPath($path).StartsWith($distFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package output escaped dist: $path"
    }
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}
New-Item -ItemType Directory -Force -Path $package, $builder | Out-Null

$files = @(
    'scripts\build-pc.ps1',
    'targets\mkvsdcu\private\rexglue-host\CMakeLists.txt',
    'targets\mkvsdcu\private\rexglue-host\CMakePresets.json',
    'targets\mkvsdcu\private\rexglue-host\mkvsdcu_manifest.toml',
    'targets\mkvsdcu\private\rexglue-host\config\mkvsdcu_functions.toml',
    'targets\mkvsdcu\private\rexglue-host\config\mkvsdcu_codegen.toml',
    'targets\mkvsdcu\private\rexglue-host\src\main.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\mkvsdcu_app.h',
    'targets\mkvsdcu\private\rexglue-host\src\mkvsdcu_app.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\port_host.h',
    'targets\mkvsdcu\private\rexglue-host\src\host_tweaks.h',
    'targets\mkvsdcu\private\rexglue-host\src\host_tweaks.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\settings_catalog.h',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\settings_catalog.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\port_menu.h',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\port_menu.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\port_config.h',
    'targets\mkvsdcu\private\rexglue-host\src\port_menu\port_config.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\input\controller_filter.h',
    'targets\mkvsdcu\private\rexglue-host\src\input\controller_filter.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\frame_telemetry.h',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\frame_telemetry.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\system_telemetry.h',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\system_telemetry.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\perf_overlay.h',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\perf_overlay.cpp',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\guest_profiler.h',
    'targets\mkvsdcu\private\rexglue-host\src\telemetry\guest_profiler.cpp'
)
foreach ($relative in $files) {
    $source = Join-Path $root $relative
    $target = Join-Path $builder $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing builder file: $source" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
}

# Carry the pinned ReXGlue runtime, headers, CMake package, and license files.
$sdkSource = Join-Path $root 'references\rexglue-sdk\out\install\win-amd64'
$sdkTarget = Join-Path $builder 'references\rexglue-sdk\out\install\win-amd64'
if (-not (Test-Path -LiteralPath (Join-Path $sdkSource 'bin\rexglue.exe') -PathType Leaf)) {
    throw "Pinned ReXGlue SDK install is missing: $sdkSource"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sdkTarget) | Out-Null
Copy-Item -LiteralPath $sdkSource -Destination $sdkTarget -Recurse
Copy-Item -LiteralPath (Join-Path $root 'references\rexglue-sdk\LICENSE') `
    -Destination (Join-Path $builder 'references\rexglue-sdk\LICENSE')
Compress-Archive -Path (Join-Path $builder '*') -DestinationPath $builderZip -CompressionLevel Optimal

$releaseManifest = [PSCustomObject]@{
    schema = 1
    version = $Version
    platform = 'win-x64'
    supportedXexSha256 = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'
    sdkRevision = 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518'
}
$manifestPath = Join-Path $package 'release-manifest.json'
[IO.File]::WriteAllText($manifestPath, ($releaseManifest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

& dotnet publish (Join-Path $root 'launcher\MKVDCU.Updater\MKVDCU.Updater.csproj') `
    -c Release -r win-x64 --self-contained true '-p:PublishSingleFile=true' `
    '-p:IncludeNativeLibrariesForSelfExtract=true' -o $updater
if ($LASTEXITCODE -ne 0) { throw 'Updater publish failed.' }
$helper = Join-Path $updater 'MKVDCU.Updater.exe'
if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { throw 'Published updater EXE is missing.' }

& dotnet publish (Join-Path $root 'launcher\MKVDCU.Launcher\MKVDCU.Launcher.csproj') `
    -c Release -r win-x64 --self-contained true '-p:PublishSingleFile=true' `
    '-p:IncludeNativeLibrariesForSelfExtract=true' "-p:BuilderBundlePath=$builderZip" `
    "-p:UpdaterBinaryPath=$helper" "-p:ReleaseManifestPath=$manifestPath" -o $published
if ($LASTEXITCODE -ne 0) { throw 'Launcher publish failed.' }
$launcher = Join-Path $published 'MKVDCU.Launcher.exe'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw 'Published launcher EXE is missing.' }
Copy-Item -LiteralPath $launcher -Destination $standalone
Copy-Item -LiteralPath $launcher -Destination (Join-Path $package 'MKVDCU-Recomp.exe')

Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -CompressionLevel Optimal

$zipStream = [IO.File]::OpenRead($zip)
$sha256 = [Security.Cryptography.SHA256]::Create()
try { $hash = [BitConverter]::ToString($sha256.ComputeHash($zipStream)).Replace('-', '') }
finally { $zipStream.Dispose(); $sha256.Dispose() }
Write-Host "[OK] Single EXE: $standalone"
Write-Host "[OK] Update package: $zip"
Write-Host "[OK] Update package SHA-256: $hash"
Write-Warning 'The EXE includes the launcher, port build sources, updater, and pinned ReXGlue SDK. The first build still needs local LLVM/Clang, CMake, Ninja, and Microsoft C++ Build Tools with the Windows SDK.'
