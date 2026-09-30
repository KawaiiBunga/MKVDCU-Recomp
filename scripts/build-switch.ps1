[CmdletBinding()]
param([ValidateRange(1, 64)][int]$Parallel = 3)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $root 'references/nfsmw-nx-main/sdk'
$patch = Join-Path $root 'patches/rexglue-sdk/vulkan-common.patch'
$toolchain = Join-Path $root 'references/nfsmw-nx-main/tools/switch/cmake/switch-devkitA64.cmake'
$mesa = Join-Path $root 'mesa-sdk/opt/devkitpro/portlibs/switch'
$source = Join-Path $root 'targets/mkvsdcu-nx'
$build = Join-Path $source 'build-switch'
$cmake = if (Test-Path -LiteralPath 'C:/Program Files/CMake/bin/cmake.exe') {
    'C:/Program Files/CMake/bin/cmake.exe'
} else { (Get-Command cmake.exe -ErrorAction Stop).Source }
if ($cmake -match '[\\/](msys64|msys2)[\\/]') { throw 'Install native Windows CMake; MSYS CMake rewrites Windows source paths.' }
foreach ($required in @($sdk, $patch, $toolchain, $mesa)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing Switch dependency: $required" }
}
# Run from the repository root with an explicit prefix: the vendor SDK is
# ignored and is not itself a Git checkout.
$prefix = 'references/nfsmw-nx-main/sdk'
& git -C $root apply "--directory=$prefix" --check $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    & git -C $root apply "--directory=$prefix" $patch
    if ($LASTEXITCODE -ne 0) { throw 'Failed to apply shared Vulkan fixes to Switch SDK.' }
} else {
    & git -C $root apply "--directory=$prefix" --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'Switch SDK conflicts with vulkan-common.patch; preserve vendor changes and resolve the affected hunks.' }
}
$cachedNinja = $null
$cacheFile = Join-Path $build 'CMakeCache.txt'
if (Test-Path -LiteralPath $cacheFile) {
    $makeLine = Get-Content -LiteralPath $cacheFile | Where-Object { $_ -match '^CMAKE_MAKE_PROGRAM:FILEPATH=' } | Select-Object -First 1
    if ($makeLine) { $cachedNinja = $makeLine.Substring('CMAKE_MAKE_PROGRAM:FILEPATH='.Length) }
}
$ninjaCandidates = @(
    $cachedNinja,
    (Join-Path $env:LOCALAPPDATA 'MKVDCU-Recomp/tools/llvm22.1.8-cmake4.4.3-ninja1.13.2/ninja/ninja.exe'),
    'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe',
    (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source)
$ninja = $ninjaCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) -and $_ -notmatch '[\\/](msys64|msys2)[\\/]' } | Select-Object -First 1
if (-not $ninja) { throw 'Install a native Windows Ninja; MSYS Ninja cannot run the Windows SDK commands.' }
$env:Path = (Split-Path -Parent $ninja) + ';' + $env:Path

& $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$($toolchain.Replace('\','/'))" "-DREXGLUE_SWITCH_NVK_SDK=$($mesa.Replace('\','/'))" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Switch CMake configure failed.' }
& $cmake --build $build --target mkvsdcu --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'Switch build failed.' }
$nro = Join-Path $build 'mkvsdcu.nro'
if (-not (Test-Path -LiteralPath $nro -PathType Leaf)) { throw 'Switch build did not produce mkvsdcu.nro.' }
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $dist 'mkvsdcu.nro') -Force
Copy-Item -LiteralPath (Join-Path $source 'mkvsdcu.toml') -Destination (Join-Path $dist 'mkvsdcu.toml') -Force
Write-Host "[OK] Built $nro and dist/mkvsdcu.nro with its Vulkan configuration"
