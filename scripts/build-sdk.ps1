[CmdletBinding()]
param(
    [ValidateRange(1, 64)][int]$Parallel = 4
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $root 'references\rexglue-sdk'
$patch = Join-Path $root 'patches\rexglue-sdk\sdk.patch'
$install = Join-Path $sdk 'out\install\win-amd64'
$git = if (Test-Path -LiteralPath 'C:\Program Files\Git\cmd\git.exe' -PathType Leaf) {
    'C:\Program Files\Git\cmd\git.exe'
} else { (Get-Command git -ErrorAction Stop).Source }
$cmake = if (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe' -PathType Leaf) {
    'C:\Program Files\CMake\bin\cmake.exe'
} else { (Get-Command cmake -ErrorAction Stop).Source }
$env:Path = 'C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;' + $env:Path

$gitlink = & $git -C $root ls-files --stage -- references/rexglue-sdk
if ($LASTEXITCODE -ne 0 -or $gitlink -notmatch '^160000 ([0-9a-f]{40}) ') {
    throw 'The ReXGlue SDK is not registered as a submodule. Stage its gitlink first.'
}
$expectedRevision = $Matches[1]
if (-not (Test-Path -LiteralPath (Join-Path $sdk '.git'))) {
    & $git -C $root submodule update --init -- references/rexglue-sdk
    if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the ReXGlue SDK submodule.' }
}
$actualRevision = (& $git -C $sdk rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $expectedRevision) {
    throw "SDK checkout is at $actualRevision; the parent repository pins $expectedRevision. Commit an updated gitlink or check out the pinned revision."
}

$nestedSource = Join-Path $sdk 'thirdparty\libmspack\libmspack\mspack\lzxd.c'
if (-not (Test-Path -LiteralPath $nestedSource -PathType Leaf)) {
    & $git -C $sdk submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the SDK dependencies.' }
}

& $git -C $sdk apply --check $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    & $git -C $sdk apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'Failed to apply the SDK patch.' }
} else {
    & $git -C $sdk apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) { throw 'The SDK source conflicts with patches/rexglue-sdk/sdk.patch.' }
}

# A release must contain precisely the renderer source represented by the
# parent commit. This catches edits to the nested checkout that were not
# exported into the patch before packaging an SDK binary.
$currentDiff = ((& $git -C $sdk diff --binary --no-ext-diff HEAD --) -join "`n").TrimEnd("`r", "`n")
$recordedDiff = ([IO.File]::ReadAllText($patch) -replace "`r`n", "`n").TrimEnd("`r", "`n")
if ($LASTEXITCODE -ne 0 -or $currentDiff -cne $recordedDiff) {
    throw 'SDK source differs from sdk.patch. Export the complete SDK diff into patches/rexglue-sdk/sdk.patch before building a release.'
}
$newFiles = @(& $git -C $sdk ls-files --others --exclude-standard)
if ($LASTEXITCODE -ne 0 -or $newFiles.Count -gt 0) {
    throw "Unrecorded files in the SDK checkout: $($newFiles -join ', '). Add them to sdk.patch before building a release."
}

foreach ($tool in @('clang.exe', 'clang++.exe', 'ninja.exe', 'llvm-rc.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. See docs/PC_BUILD.md." }
}

Push-Location $sdk
try {
    & $cmake --preset win-amd64 '-DREXGLUE_USE_VULKAN=ON' '-DREXGLUE_USE_D3D12=ON' '-DREXGLUE_ENABLE_FIDELITYFX=ON' '-DREXGLUE_ENABLE_TRACY=ON' '-DREXGLUE_ENABLE_PERF_COUNTERS=ON'
    if ($LASTEXITCODE -ne 0) { throw "SDK configure failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

# FidelityFX ships this resource in UTF-16LE. llvm-rc requires UTF-8, so
# normalize the fetched dependency after CMake populates it and before build.
$resource = Join-Path $sdk 'out\build\win-amd64\_deps\fidelityfx-src\ffx-api\src\resource\ffx_api_dll.rc'
if (-not (Test-Path -LiteralPath $resource -PathType Leaf)) { throw "Missing FidelityFX resource: $resource" }
$bytes = [IO.File]::ReadAllBytes($resource)
if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xff -and $bytes[1] -eq 0xfe) {
    $utf16 = [Text.UnicodeEncoding]::new($false, $true, $true)
    $resourceText = $utf16.GetString($bytes, 2, $bytes.Length - 2)
    [IO.File]::WriteAllText($resource, "#pragma code_page(65001)`r`n" + $resourceText, [Text.UTF8Encoding]::new($false))
} elseif (-not ([Text.Encoding]::UTF8.GetString($bytes)).StartsWith('#pragma code_page(65001)')) {
    throw "Unexpected FidelityFX resource encoding: $resource"
}

& $cmake --build (Join-Path $sdk 'out\build\win-amd64') --config Release --target install --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw "SDK build/install failed with exit code $LASTEXITCODE" }
$exe = Join-Path $install 'bin\rexglue.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "SDK install did not create $exe" }
$stamp = [ordered]@{
    sdkRevision = $expectedRevision
    patchSha256 = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash
    rexglueSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
}
[IO.File]::WriteAllText((Join-Path $install 'mkvdcu-sdk-build.json'), ($stamp | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Host "[OK] ReXGlue SDK built and installed from $expectedRevision plus sdk.patch."
