[CmdletBinding()]
param(
    # Release version without the leading v, e.g. 0.2.0 or 0.3.0-preview.1
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$')][string]$Version,
    # Markdown shown in the launcher's "What's new" and on the GitHub release.
    [string]$NotesFile = '',
    # Create the GitHub release with gh (otherwise only build the files).
    [switch]$Publish,
    # Mark the release as a preview (only the launcher's Preview channel sees it).
    [switch]$Prerelease
)

# Builds everything one release needs into dist\release\<version>:
#   MKVDCU-Recomp.exe       the launcher players download
#   mkvdcu-port-<ver>.zip   host sources, configs and the pinned ReXGlue SDK
#   release.json            versions, sizes and SHA-256 of every file
#   mkvdcu-toolchain-*.zip  only when no earlier release already carries it
# Test a release locally before publishing:
#   $env:MKVDCU_RELEASE_DIR = 'dist\release\<version>'; .\dist\release\<version>\MKVDCU-Recomp.exe
# Optional code signing: set MKVDCU_SIGN to a command that signs the file
# passed as its last argument, e.g.
#   signtool sign /fd sha256 /tr http://timestamp.acs.microsoft.com /td sha256 /dlib ... /dmdf ...
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
$repo = 'KawaiiBunga/MKVDCU-Recomp'
$out = Join-Path $root "dist\release\$Version"
$hostRel = 'targets\mkvsdcu\private\rexglue-host'
$sdkRel = 'references\rexglue-sdk\out\install\win-amd64'
$xexHash = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'

if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Get-Sha256($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
function Get-Asset($path, $extra = @{}) {
    $asset = [ordered]@{ file = (Split-Path -Leaf $path); sha256 = (Get-Sha256 $path); size = (Get-Item -LiteralPath $path).Length }
    foreach ($key in $extra.Keys) { $asset[$key] = $extra[$key] }
    return $asset
}

$dirty = & git -C $root status --porcelain -- $hostRel 2>$null
if ($dirty) { Write-Warning "Uncommitted changes under $hostRel are included in this release." }

# ---- Port package ------------------------------------------------------------
$sdk = Join-Path $root $sdkRel
if (-not (Test-Path -LiteralPath (Join-Path $sdk 'bin\rexglue.exe'))) { throw "ReXGlue SDK install missing: $sdk" }
$staging = Join-Path $out '_port'
$hostDir = Join-Path $root $hostRel
Get-ChildItem -LiteralPath $hostDir -Recurse -File | Where-Object {
    $relative = $_.FullName.Substring($hostDir.Length + 1).Replace('\', '/')
    -not ($relative -match '^(out|generated)/' -or $relative -match '(^|/)\.' -or $relative -like '*.local.toml')
} | ForEach-Object {
    $target = Join-Path $staging ($hostRel + $_.FullName.Substring($hostDir.Length))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $target
}
foreach ($required in @('CMakeLists.txt', 'config\mkvsdcu_functions.toml', 'config\mkvsdcu_codegen.toml', 'src\main.cpp')) {
    if (-not (Test-Path -LiteralPath (Join-Path $staging "$hostRel\$required"))) { throw "Port package is missing $required" }
}
Copy-Item -LiteralPath $sdk -Destination (Join-Path $staging $sdkRel) -Recurse
Copy-Item -LiteralPath (Join-Path $root 'references\rexglue-sdk\LICENSE') -Destination (Join-Path $staging 'references\rexglue-sdk\LICENSE')
$sdkRevision = (& git -C (Join-Path $root 'references\rexglue-sdk') rev-parse HEAD 2>$null)
[ordered]@{ version = $Version; sdkRevision = "$sdkRevision"; xexSha256 = $xexHash } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $staging 'port.json') -Encoding ASCII
$portZip = Join-Path $out "mkvdcu-port-$Version.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($staging, $portZip, [IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item -LiteralPath $staging -Recurse -Force

# ---- Launcher ------------------------------------------------------------------
$publishDir = Join-Path $out '_launcher'
& dotnet publish (Join-Path $root 'launcher\MKVDCU.Launcher\MKVDCU.Launcher.csproj') -c Release -o $publishDir "-p:Version=$Version"
if ($LASTEXITCODE -ne 0) { throw 'Launcher publish failed.' }
$exe = Join-Path $out 'MKVDCU-Recomp.exe'
Move-Item -LiteralPath (Join-Path $publishDir 'MKVDCU-Recomp.exe') -Destination $exe
Remove-Item -LiteralPath $publishDir -Recurse -Force
if ($env:MKVDCU_SIGN) {
    & cmd /c "$env:MKVDCU_SIGN `"$exe`""
    if ($LASTEXITCODE -ne 0) { throw 'Signing failed.' }
} else {
    Write-Warning 'MKVDCU_SIGN is not set: the launcher is unsigned (see docs/RELEASING.md).'
}

# ---- Toolchain -----------------------------------------------------------------
$toolchainZip = Get-ChildItem -LiteralPath (Join-Path $root 'dist') -Filter 'mkvdcu-toolchain-*.zip' | Select-Object -First 1
if (-not $toolchainZip) {
    & (Join-Path $PSScriptRoot 'make-toolchain.ps1') | Out-Null
    $toolchainZip = Get-ChildItem -LiteralPath (Join-Path $root 'dist') -Filter 'mkvdcu-toolchain-*.zip' | Select-Object -First 1
}
$toolchainId = $toolchainZip.BaseName.Substring('mkvdcu-toolchain-'.Length)
$toolchain = Get-Asset $toolchainZip.FullName @{ id = $toolchainId }
# An earlier release that already carries this exact archive saves a 130 MB upload.
$existingUrl = $null
if ($Publish) {
    $releases = & gh api "repos/$repo/releases?per_page=30" | ConvertFrom-Json
    foreach ($release in $releases) {
        $match = $release.assets | Where-Object { $_.name -eq $toolchainZip.Name } | Select-Object -First 1
        if ($match) { $existingUrl = $match.browser_download_url; break }
    }
}
if ($existingUrl) {
    $toolchain.url = $existingUrl
} else {
    Copy-Item -LiteralPath $toolchainZip.FullName -Destination $out
}

# ---- Manifest --------------------------------------------------------------------
$notes = if ($NotesFile) { Get-Content -LiteralPath $NotesFile -Raw } else { "MKVDCU-Recomp $Version" }
$manifest = [ordered]@{
    schema = 1
    version = $Version
    notes = $notes
    xexSha256 = $xexHash
    launcher = Get-Asset $exe
    port = Get-Asset $portZip
    toolchain = $toolchain
}
$manifestPath = Join-Path $out 'release.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))

$assets = Get-ChildItem -LiteralPath $out -File | ForEach-Object FullName
if ($Publish) {
    $notesPath = Join-Path $out '_notes.md'
    Set-Content -LiteralPath $notesPath -Value $notes -Encoding UTF8
    $ghArgs = @('release', 'create', "v$Version", '--repo', $repo, '--title', "MKVDCU-Recomp $Version", '--notes-file', $notesPath)
    if ($Prerelease) { $ghArgs += '--prerelease' }
    & gh @ghArgs @assets
    if ($LASTEXITCODE -ne 0) { throw 'gh release create failed.' }
    Remove-Item -LiteralPath $notesPath
}
Get-ChildItem -LiteralPath $out -File | Select-Object Name, @{ n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
