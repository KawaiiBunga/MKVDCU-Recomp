[CmdletBinding()]
param(
    [string]$Python = '',
    [ValidateRange(1, 32)][int]$Parallel = 4,
    [string]$ShaderPackage = '',
    [ValidateSet('off','generate','use')][string]$Pgo = 'off',
    [string]$PgoDirectory = 'E:/tools/pgo/mkvsdcu-v15',
    [ValidateRange(0, 256)][int]$PgoInlineLimit = 0,
    [ValidateRange(0, 100)][int]$PgoInlineGrowth = 0,
    [ValidateRange(0, 1024)][int]$PgoPartitions = 0,
    [switch]$PgoIpaPta,
    [switch]$CpuOptimizations
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
    $pgoHook = ''
    $experiment = ''
    if ($Pgo -eq 'use') {
        $tag = "inline-$PgoInlineLimit-growth-$PgoInlineGrowth-partitions-$PgoPartitions-pta-$([int][bool]$PgoIpaPta)"
        $experiment = Join-Path $PgoDirectory "experiments/$tag"
        $experimentArgs = @('scripts/prepare-switch-pgo-experiment.py', '--output', $experiment,
            '--inline-limit', $PgoInlineLimit, '--growth', $PgoInlineGrowth, '--partitions', $PgoPartitions)
        if ($PgoIpaPta) { $experimentArgs += '--ipa-pta' }
        & $Python @experimentArgs
        if ($LASTEXITCODE) { throw 'PGO experiment preparation failed' }
        $pgoHook = Join-Path $experiment 'options.cmake'
    } elseif ($PgoInlineLimit -or $PgoInlineGrowth -or $PgoPartitions -or $PgoIpaPta) {
        throw 'PGO tuning options require -Pgo use and real captured training data'
    }
    & $cmake -S targets/mkvsdcu-nx -B $build -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_TOOLCHAIN_FILE=$root/switch/cmake/devkitA64-rex-deko3d.cmake" `
        "-DCMAKE_INSTALL_PREFIX=$tools/install/mkvsdcu-deko3d" `
        "-DREXSDK_DIR=$root/references/mkvsdcu-deko-sdk" `
        "-DPython3_EXECUTABLE=$Python" -DCMAKE_BUILD_TYPE=Release `
        -DMKVSDCU_DEKO3D=ON -DMKVSDCU_DIRECT_CALLS=ON -DMKVSDCU_LTO=ON `
        "-DMKVSDCU_CPU_OPTIMIZATIONS=$([int][bool]$CpuOptimizations)" `
        "-DMKVSDCU_PGO=$Pgo" "-DMKVSDCU_PGO_DIR=$PgoDirectory" `
        "-DCMAKE_PROJECT_mkvsdcu_INCLUDE=$pgoHook" `
        "-DMKVSDCU_LTO_JOBS=$Parallel" `
        -DREXGLUE_BUILD_TESTS=OFF -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_RECOMP_DEBUG_INFO=none
    if ($LASTEXITCODE) { throw 'Native Switch configure failed' }
    & $cmake --build $build --target mkvsdcu --parallel $Parallel
    if ($LASTEXITCODE) { throw 'Native Switch build failed' }
    & $Python scripts/verify-switch-build.py --build $build `
        --nm "$env:DEVKITA64/bin/aarch64-none-elf-nm.exe" --deko3d
    if ($LASTEXITCODE) { throw 'Native Switch artifact verification failed' }
    if ($Pgo -ne 'off') {
        & $Python scripts/switch-pgo.py verify --build $build --mode $Pgo --profile $PgoDirectory
        if ($LASTEXITCODE) { throw 'PGO artifact verification failed' }
    }
    if ($Pgo -eq 'use') {
        $dumps = @(Get-ChildItem -LiteralPath $build -Filter 'mkvsdcu.wpa.*.inline' -File)
        if ($dumps.Count -ne 1) { throw 'Expected exactly one GCC whole-program inliner dump' }
        Copy-Item -LiteralPath $dumps[0].FullName -Destination "$experiment/inline.txt" -Force
        & $Python scripts/analyze-switch-pgo.py --profile $PgoDirectory `
            --dump "$experiment/inline.txt" --elf "$build/mkvsdcu" --output "$experiment/analysis.json"
        if ($LASTEXITCODE) { throw 'PGO coverage/inlining analysis failed' }
        Copy-Item -LiteralPath "$build/mkvsdcu.nro","$build/switch-build-verification.json" -Destination $experiment -Force
    }
    $dist = Join-Path $root 'dist/deko3d'
    if ($Pgo -eq 'generate') { $dist = Join-Path $root 'dist/deko3d-pgo-training' }
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
