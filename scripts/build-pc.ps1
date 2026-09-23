[CmdletBinding()]
param(
    [ValidateRange(1, 64)][int]$Parallel = 4
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Join-Path $workspaceRoot 'targets\mkvsdcu\private\rexglue-host'
$manifest = Join-Path $projectRoot 'mkvsdcu_manifest.toml'
$config = Join-Path $projectRoot 'config\mkvsdcu_functions.toml'
$gameXex = Join-Path $workspaceRoot 'user-game-files\work\mkvsdcu\default.xex'
$buildDir = Join-Path $projectRoot 'out\build\win-amd64-release'
$sdkInstall = Join-Path $workspaceRoot 'references\rexglue-sdk\out\install\win-amd64'
$rexglue = Join-Path $sdkInstall 'bin\rexglue.exe'
$cmake = if (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe') {
    'C:\Program Files\CMake\bin\cmake.exe'
} else {
    (Get-Command cmake -ErrorAction Stop).Source
}

foreach ($required in @($manifest, $config, $gameXex, $rexglue, $cmake)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file is missing: $required. See docs/PC_BUILD.md."
    }
}
$expectedXexHash = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'
$actualXexHash = (Get-FileHash -LiteralPath $gameXex -Algorithm SHA256).Hash
if ($actualXexHash -ne $expectedXexHash) {
    throw "Staged XEX SHA-256 $actualXexHash does not match the function config's $expectedXexHash. Do not build against a different executable revision."
}

& $rexglue codegen $manifest
if ($LASTEXITCODE -ne 0) { throw "ReXGlue codegen failed with exit code $LASTEXITCODE" }

if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    Push-Location $projectRoot
    try {
        & $cmake --preset win-amd64-release "-DCMAKE_PREFIX_PATH=$sdkInstall"
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
}

& $cmake --build $buildDir --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw "PC build failed with exit code $LASTEXITCODE" }

$exe = Join-Path $buildDir 'mkvsdcu.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build did not create $exe" }
Write-Host "[OK] PC game built: $exe"
