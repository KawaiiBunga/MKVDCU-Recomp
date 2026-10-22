[CmdletBinding()]
param(
    [string]$ToolsRoot = 'E:/tools',
    [ValidateRange(1, 16)][int]$Parallel = 4
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = [IO.Path]::GetFullPath($ToolsRoot)
if (-not $tools.StartsWith('E:\tools', [StringComparison]::OrdinalIgnoreCase) -or
    ($tools.Length -gt 8 -and $tools[8] -ne '\')) {
    throw 'Keep portable tools and installation caches under E:\tools.'
}
$env:TEMP = Join-Path $tools 'tmp'
$env:TMP = $env:TEMP
$llvm = Join-Path $tools 'llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin'
$sdk = Join-Path $tools 'windows-sdk'
$vc = Join-Path $tools 'msvc/VC/Tools/MSVC/14.44.35207'
$env:Path = "$tools/msvc-bin;$llvm;$tools/ninja;$sdk/bin/10.0.26100.0/x64;" + $env:Path
$env:INCLUDE = "$vc/include;$sdk/Include/10.0.26100.0/ucrt;$sdk/Include/10.0.26100.0/shared;$sdk/Include/10.0.26100.0/um;$sdk/Include/10.0.26100.0/winrt;$sdk/Include/10.0.26100.0/cppwinrt"
$env:LIB = "$vc/lib/x64;$sdk/Lib/10.0.26100.0/ucrt/x64;$sdk/Lib/10.0.26100.0/um/x64"
$cmake = Join-Path $tools 'cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
$rexSdk = Join-Path $root 'references/rexglue-sdk/out/install/win-amd64'
$generator = Join-Path $rexSdk 'bin/rexglue.exe'
$game = Join-Path $root 'user-game-files/work/mkvsdcu'
$source = Join-Path $root 'targets/mkvsdcu/private/rexglue-host'
$stage = Join-Path $root 'build/d3d-trace-host'
$build = Join-Path $root 'build/d3d-trace-pc'
foreach ($file in @($cmake, $generator, "$llvm/clang++.exe", "$tools/msvc-bin/lld-link.exe",
                     "$vc/lib/x64/msvcprt.lib", "$sdk/Lib/10.0.26100.0/ucrt/x64/ucrt.lib")) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing portable PC prerequisite: $file" }
}
$xex = Join-Path $game 'default.xex'
if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash -ne
    '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7') {
    throw 'Game executable does not match the verified D3D map.'
}
$sentinel = Join-Path $stage '.mkvsdcu-trace-stage'
if ((Test-Path -LiteralPath $stage) -and -not (Test-Path -LiteralPath $sentinel)) {
    throw "Refusing to overwrite an unrelated directory: $stage"
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Set-Content -LiteralPath $sentinel -Value 'Isolated PC diagnostic source copy; no game renderer replacement.'
Copy-Item -LiteralPath (Join-Path $source 'CMakeLists.txt') -Destination $stage -Force
foreach ($directory in @('src', 'config')) {
    $destination = Join-Path $stage $directory
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $source $directory) | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $destination -Recurse -Force
    }
}
$gameLiteral = ConvertTo-Json -InputObject $game.Replace('\', '/') -Compress
$xexLiteral = ConvertTo-Json -InputObject $xex.Replace('\', '/') -Compress
$manifest = Join-Path $stage 'mkvsdcu_manifest.toml'
$text = @"
[project]
name = "mkvsdcu"
sdk_version = "0.10.0"
game_root = $gameLiteral

[entrypoint]
file_path = $xexLiteral
out_directory_path = "generated/default"
includes = ["config/mkvsdcu_functions.toml", "config/mkvsdcu_codegen.toml"]
"@
if (-not (Test-Path -LiteralPath $manifest) -or (Get-Content -LiteralPath $manifest -Raw).Trim() -ne $text.Trim()) {
    [IO.File]::WriteAllText($manifest, $text, [Text.UTF8Encoding]::new($false))
}
$stamp = Join-Path $stage 'generated/default/codegen.build.stamp'
$inputs = @($manifest, $generator, $xex) + @(Get-ChildItem -LiteralPath (Join-Path $stage 'config') -Recurse -File | ForEach-Object FullName)
$regenerate = -not (Test-Path -LiteralPath $stamp)
if (-not $regenerate) {
    $stampTime = (Get-Item -LiteralPath $stamp).LastWriteTimeUtc
    $regenerate = [bool]($inputs | Where-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $stampTime } | Select-Object -First 1)
}
if ($regenerate) {
    & $generator codegen $manifest
    if ($LASTEXITCODE -ne 0) { throw 'Isolated PC trace code generation failed.' }
}
& $cmake -S $stage -B $build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$tools/ninja/ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/cmake/msvc-portable.cmake" `
    "-DMKVSDCU_TOOLS_ROOT=$tools" "-DCMAKE_PREFIX_PATH=$rexSdk" `
    -DCMAKE_BUILD_TYPE=Release -DMKVSDCU_D3D_TRACE=ON
if ($LASTEXITCODE -ne 0) { throw 'Portable PC trace configure failed.' }
& $cmake --build $build --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'Portable PC trace build failed.' }
Write-Host "Built $build/mkvsdcu.exe (original renderer with optional D3D capture)."
