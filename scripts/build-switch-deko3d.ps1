[CmdletBinding()]
param(
    [string]$Python = '',
    [ValidateRange(1, 32)][int]$Parallel = 4,
    [string]$ShaderPackage = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = 'E:/tools'
$env:TEMP = "$tools/tmp"
$env:TMP = $env:TEMP
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONPATH = "$tools/python-packages"
$env:DEVKITPRO = "$tools/devkitPro"
$env:DEVKITA64 = "$env:DEVKITPRO/devkitA64"
$env:MAKE = "$tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin/mingw32-make.exe"
$env:PATH = "$env:DEVKITA64/bin;$env:DEVKITPRO/tools/bin;$tools/ninja;" + $env:PATH
$cmake = "$tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe"
$ninja = "$tools/ninja/ninja.exe"
if (-not $Python) { $Python = (Get-Command python.exe -ErrorAction Stop).Source }
foreach ($file in @($Python,$cmake,$ninja,$env:MAKE,"$env:DEVKITA64/bin/aarch64-none-elf-g++.exe")) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing existing tool: $file" }
}
Push-Location $root
try {
    & $Python scripts/prepare-deko3d-sdk.py
    if ($LASTEXITCODE) { throw 'Native SDK preparation failed' }
    $build = Join-Path $root 'build/switch-deko3d'
    & $cmake -S targets/mkvsdcu-nx -B $build -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_TOOLCHAIN_FILE=$root/switch/cmake/devkitA64-rex-deko3d.cmake" `
        "-DCMAKE_INSTALL_PREFIX=$tools/install/mkvsdcu-deko3d" `
        "-DREXSDK_DIR=$root/references/mkvsdcu-deko-sdk" `
        "-DPython3_EXECUTABLE=$Python" -DCMAKE_BUILD_TYPE=Release `
        -DMKVSDCU_DEKO3D=ON -DMKVSDCU_DIRECT_CALLS=ON -DMKVSDCU_LTO=ON `
        "-DMKVSDCU_LTO_JOBS=$Parallel" `
        -DREXGLUE_BUILD_TESTS=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_RECOMP_DEBUG_INFO=none
    if ($LASTEXITCODE) { throw 'Native Switch configure failed' }
    & $cmake --build $build --target mkvsdcu --parallel $Parallel
    if ($LASTEXITCODE) { throw 'Native Switch build failed' }
    & $Python scripts/verify-switch-build.py --build $build `
        --nm "$env:DEVKITA64/bin/aarch64-none-elf-nm.exe" --deko3d
    if ($LASTEXITCODE) { throw 'Native Switch artifact verification failed' }
    $dist = Join-Path $root 'dist/deko3d'
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    Copy-Item -LiteralPath "$build/mkvsdcu.nro","$build/switch-build-verification.json" -Destination $dist -Force
    Copy-Item -LiteralPath "$root/targets/mkvsdcu-nx/config/deko3d-defaults.toml" -Destination "$dist/mkvsdcu.toml" -Force
    if ($ShaderPackage) {
        foreach ($name in @('mkvsdcu_shaders_dk.nfsp','mkvsdcu_shadercache_dksh.bin','mkvsdcu_shader_limits.bin','shader-verification.json')) {
            Copy-Item -LiteralPath (Join-Path $ShaderPackage $name) -Destination $dist -Force
        }
    }
    Write-Host "Verified native deko3d NRO: $dist/mkvsdcu.nro"
} finally { Pop-Location }
