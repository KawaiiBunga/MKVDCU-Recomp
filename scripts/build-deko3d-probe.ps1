[CmdletBinding()]
param(
    [string]$ToolsRoot = 'E:/tools',
    [string]$Python = '',
    [ValidateRange(1, 16)][int]$Parallel = 3
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = [IO.Path]::GetFullPath($ToolsRoot)
if (-not $tools.StartsWith('E:\tools', [StringComparison]::OrdinalIgnoreCase) -or
    ($tools.Length -gt 8 -and $tools[8] -ne '\')) {
    throw 'This task keeps tools, caches and installation temp files under E:\tools.'
}
$env:TEMP = Join-Path $tools 'tmp'
$env:TMP = $env:TEMP
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:DEVKITPRO = (Join-Path $tools 'devkitPro').Replace('\', '/')
$cmake = Get-ChildItem -LiteralPath (Join-Path $tools 'cmake') -Directory |
    Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'bin/cmake.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$ninja = Join-Path $tools 'ninja/ninja.exe'
$uam = Join-Path $tools 'build/uam/uam.exe'
if (-not $Python) {
    $Python = (Get-Command python.exe -ErrorAction Stop).Source
    if ($Python -match 'WindowsApps') { throw 'Pass -Python with a working installed Python executable.' }
}
foreach ($file in @($cmake, $ninja, $uam, $Python, "$env:DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++.exe")) {
    if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing native build tool: $file" }
}
$build = Join-Path $root 'build/deko3d-probe'
& $cmake -S (Join-Path $root 'switch/deko3d-probe') -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $root 'switch/cmake/devkitA64-native.cmake')" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" "-DDEVKITPRO=$env:DEVKITPRO" "-DUAM=$uam" `
    "-DPython3_EXECUTABLE=$Python" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'deko3d probe configure failed' }
& $cmake --build $build --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'deko3d probe build failed' }
$env:Path = "$env:DEVKITPRO/devkitA64/bin;" + $env:Path
$nm = "$env:DEVKITPRO/devkitA64/bin/aarch64-none-elf-nm.exe"
$elf = Join-Path $build 'mkvsdcu-deko3d-probe'
$symbols = & $nm --defined-only $elf
if ($LASTEXITCODE -ne 0 -or -not ($symbols -match '^0+ T _start$')) { throw 'Probe entry point is not zero' }
if (-not ($symbols -match ' dkQueueSubmitCommands$') -or ($symbols -match ' vkCreateInstance$| nvk_')) {
    throw 'Probe did not link a deko3d-only renderer'
}
$nro = Join-Path $build 'mkvsdcu-deko3d-probe.nro'
$bytes = [IO.File]::ReadAllBytes($nro)
if ([Text.Encoding]::ASCII.GetString($bytes, 0x10, 4) -ne 'NRO0') { throw 'Invalid NRO header' }
$imageSize = [BitConverter]::ToUInt32($bytes, 0x18)
if ([Text.Encoding]::ASCII.GetString($bytes, $imageSize, 4) -ne 'ASET') { throw 'Missing NRO assets' }
$report = [ordered]@{
    artifact = 'mkvsdcu-deko3d-probe.nro'
    graphics_api = 'deko3d'
    game_renderer = $false
    hardware_tested = $false
    bytes = $bytes.Length
    sha256 = (Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash
    entry_point = '0'
    native_windows_build = $true
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build 'verification.json')
$dist = Join-Path $root 'dist/deko3d-probe'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath $nro -Destination $dist -Force
Copy-Item -LiteralPath (Join-Path $build 'verification.json') -Destination $dist -Force
Write-Host "Built and verified $dist/mkvsdcu-deko3d-probe.nro (platform test, not the game)."
