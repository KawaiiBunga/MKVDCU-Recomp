[CmdletBinding()]
param(
    [ValidateRange(1, 64)][int]$Parallel = 4,
    [string]$GameDataRoot = '',
    [string]$SdkRoot = ''
)

$ErrorActionPreference = 'Stop'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Join-Path $workspaceRoot 'targets\mkvsdcu\private\rexglue-host'
$manifest = Join-Path $projectRoot 'mkvsdcu_manifest.local.toml'
$config = Join-Path $projectRoot 'config\mkvsdcu_functions.toml'
$codegenOptions = Join-Path $projectRoot 'config\mkvsdcu_codegen.toml'
if (-not $GameDataRoot) { $GameDataRoot = Join-Path $workspaceRoot 'user-game-files\work\mkvsdcu' }
if (-not $SdkRoot) { $SdkRoot = Join-Path $workspaceRoot 'references\rexglue-sdk' }
$GameDataRoot = [System.IO.Path]::GetFullPath($GameDataRoot)
$SdkRoot = [System.IO.Path]::GetFullPath($SdkRoot)
$gameXex = Join-Path $GameDataRoot 'default.xex'
$buildDir = Join-Path $projectRoot 'out\build\win-amd64-release'
$checkoutInstall = Join-Path $SdkRoot 'out\install\win-amd64'
$sdkInstall = if (Test-Path -LiteralPath (Join-Path $checkoutInstall 'bin\rexglue.exe') -PathType Leaf) {
    $checkoutInstall
} elseif (Test-Path -LiteralPath (Join-Path $SdkRoot 'bin\rexglue.exe') -PathType Leaf) {
    $SdkRoot
} else {
    throw "No installed ReXGlue SDK found under $SdkRoot. Choose its install folder or a checkout with out/install/win-amd64."
}
$rexglue = Join-Path $sdkInstall 'bin\rexglue.exe'
$toolDirs = @('C:\Program Files\LLVM\bin', 'C:\Program Files\CMake\bin', (Join-Path $sdkInstall 'bin')) |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$env:Path = ($toolDirs -join ';') + ';' + $env:Path
$cmake = if (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe') {
    'C:\Program Files\CMake\bin\cmake.exe'
} else {
    (Get-Command cmake -ErrorAction Stop).Source
}
foreach ($tool in @('clang.exe', 'clang++.exe', 'ninja.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing $tool. Install LLVM/Clang and Ninja, then relaunch. The launcher already includes ReXGlue."
    }
}

foreach ($required in @($config, $codegenOptions, $gameXex, $rexglue, $cmake)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file is missing: $required. See docs/PC_BUILD.md."
    }
}
$expectedXexHash = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'
$xexStream = [System.IO.File]::OpenRead($gameXex)
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $actualXexHash = [System.BitConverter]::ToString($sha256.ComputeHash($xexStream)).Replace('-', '')
} finally {
    $sha256.Dispose()
    $xexStream.Dispose()
}
if ($actualXexHash -ne $expectedXexHash) {
    throw "Staged XEX SHA-256 $actualXexHash does not match the function config's $expectedXexHash. Do not build against a different executable revision."
}

# Generate a local manifest so codegen can use a validated game folder outside
# this checkout without changing the tracked, reproducible default manifest.
$rootLiteral = ConvertTo-Json -InputObject $GameDataRoot -Compress
$xexLiteral = ConvertTo-Json -InputObject $gameXex -Compress
$manifestText = @"
[project]
name = "mkvsdcu"
sdk_version = "0.10.0"
game_root = $rootLiteral

[entrypoint]
file_path = $xexLiteral
out_directory_path = "generated/default"
includes = ["config/mkvsdcu_functions.toml", "config/mkvsdcu_codegen.toml"]
"@
$manifestChanged = -not (Test-Path -LiteralPath $manifest -PathType Leaf) -or
    (Get-Content -LiteralPath $manifest -Raw).Trim() -ne $manifestText.Trim()
if ($manifestChanged) { [System.IO.File]::WriteAllText($manifest, $manifestText, $utf8NoBom) }

$generatedHeader = Join-Path $projectRoot 'generated\default\mkvsdcu_init.h'
$codegenStamp = Join-Path $projectRoot 'generated\default\codegen.build.stamp'
$codegenNeeded = $manifestChanged -or -not (Test-Path -LiteralPath $generatedHeader -PathType Leaf) -or
    -not (Test-Path -LiteralPath $codegenStamp -PathType Leaf)
if (-not $codegenNeeded) {
    $stampTime = (Get-Item -LiteralPath $codegenStamp).LastWriteTimeUtc
    $codegenNeeded = @($config, $codegenOptions, $gameXex, $rexglue) | Where-Object {
        (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $stampTime
    } | Select-Object -First 1
}
if ($codegenNeeded) {
    & $rexglue codegen $manifest
    if ($LASTEXITCODE -ne 0) { throw "ReXGlue codegen failed with exit code $LASTEXITCODE" }
} else {
    Write-Host '[OK] Generated code matches the selected game and SDK.'
}

# ReXGlue rewrites this boilerplate during codegen. The script has already
# checked all codegen inputs, so let CMake update its stamp without scanning
# the same XEX a second time. Direct CMake users can restore SDK-managed
# codegen by configuring with -DMKVSDCU_CODEGEN_MANAGED_EXTERNALLY=OFF.
$cmakeGlue = Join-Path $projectRoot 'generated\rexglue.cmake'
$originalGlueBytes = [System.IO.File]::ReadAllBytes($cmakeGlue)
$glueText = Get-Content -LiteralPath $cmakeGlue -Raw
$defaultManifestExpr = '${CMAKE_CURRENT_SOURCE_DIR}/mkvsdcu_manifest.toml'
$localManifestExpr = '${MKVSDCU_CODEGEN_MANIFEST}'
if ($glueText.Contains($defaultManifestExpr)) {
    $glueText = $glueText.Replace($defaultManifestExpr, $localManifestExpr)
}
$codegenCommand = '    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${MKVSDCU_CODEGEN_MANIFEST}'
if ($glueText.Contains($codegenCommand)) {
    $managedCommands = @'
set(_mkvsdcu_codegen_command $<TARGET_FILE:rex::rexglue>)
set(_mkvsdcu_codegen_arguments codegen "${MKVSDCU_CODEGEN_MANIFEST}")
if(MKVSDCU_CODEGEN_MANAGED_EXTERNALLY)
    set(_mkvsdcu_codegen_command "${CMAKE_COMMAND}")
    set(_mkvsdcu_codegen_arguments -E touch "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp")
endif()
'@
    $glueText = $glueText.Replace('add_custom_command(', $managedCommands + [Environment]::NewLine + 'add_custom_command(')
    $glueText = $glueText.Replace($codegenCommand,
        '    COMMAND ${_mkvsdcu_codegen_command} ${_mkvsdcu_codegen_arguments}')
} elseif (-not $glueText.Contains('${_mkvsdcu_codegen_command}')) {
    throw 'ReXGlue generated an unexpected CMake codegen command. Update build-pc.ps1 for this SDK.'
}
if ($glueText -ne (Get-Content -LiteralPath $cmakeGlue -Raw)) {
    [System.IO.File]::WriteAllText($cmakeGlue, $glueText, $utf8NoBom)
}

try {
    Push-Location $projectRoot
    try {
        & $cmake --preset win-amd64-release "-DCMAKE_PREFIX_PATH=$sdkInstall" "-DMKVSDCU_CODEGEN_MANIFEST=$manifest" '-DMKVSDCU_CODEGEN_MANAGED_EXTERNALLY=ON'
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }

    & $cmake --build $buildDir --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw "PC build failed with exit code $LASTEXITCODE" }
} finally {
    [System.IO.File]::WriteAllBytes($cmakeGlue, $originalGlueBytes)
}

$exe = Join-Path $buildDir 'mkvsdcu.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build did not create $exe" }
Write-Host "[OK] PC game built: $exe"
