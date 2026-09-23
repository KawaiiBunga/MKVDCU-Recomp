[CmdletBinding()]
param([string]$OutDir = '')

# Builds the build-toolchain archive the launcher downloads on a player's first
# build: the parts of the official LLVM, CMake and Ninja Windows releases the
# port needs, unmodified, with their licenses. Every source download is pinned
# by SHA-256. Run it when a pin changes; attach the zip to a release and
# reference it from release.json (scripts/release.ps1 does both).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'dist' }

$llvm = @{ Version = '22.1.8'
    Url = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/clang%2Bllvm-22.1.8-x86_64-pc-windows-msvc.tar.xz'
    File = 'clang+llvm-22.1.8-x86_64-pc-windows-msvc.tar.xz'
    Sha256 = 'd96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234' }
$cmake = @{ Version = '4.4.3'
    Url = 'https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-windows-x86_64.zip'
    File = 'cmake-4.4.3-windows-x86_64.zip'
    Sha256 = '4d52ebab7193a698651639ed80d8d04fd903358843572cf44c7fd234cb7c26ab' }
$ninja = @{ Version = '1.13.2'
    Url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
    File = 'ninja-win.zip'
    Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65' }
$licenses = @{
    'LLVM-LICENSE.txt' = 'https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/LICENSE.TXT'
    'Ninja-COPYING.txt' = 'https://raw.githubusercontent.com/ninja-build/ninja/v1.13.2/COPYING'
}
# clang++, llvm-ranlib and llvm-lib are copies of clang and llvm-ar that pick
# their mode from the file name; the launcher recreates them after unpacking.
$llvmFiles = @('bin/clang.exe', 'bin/lld-link.exe', 'bin/llvm-rc.exe', 'bin/llvm-ar.exe',
    'bin/llvm-mt.exe', 'bin/clang-scan-deps.exe', 'lib/clang')

$id = "llvm$($llvm.Version)-cmake$($cmake.Version)-ninja$($ninja.Version)"
$cache = Join-Path $OutDir '_downloads'
$staging = Join-Path $OutDir "_toolchain-$id"
$zip = Join-Path $OutDir "mkvdcu-toolchain-$id.zip"
New-Item -ItemType Directory -Force -Path $cache | Out-Null
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

function Get-Pinned($item) {
    $target = Join-Path $cache $item.File
    if (-not (Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $item.Sha256) {
        Write-Host "Downloading $($item.File)"
        & curl.exe -fsSL --retry 3 -o $target $item.Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($item.Url)" }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $item.Sha256) {
            throw "SHA-256 mismatch for $($item.File)"
        }
    }
    return $target
}

# Windows' own tar cannot read this LLVM archive; Git for Windows' GNU tar can.
$gnuTar = @('C:\Program Files\Git\usr\bin\tar.exe', (Join-Path ${env:ProgramFiles(x86)} 'Git\usr\bin\tar.exe')) |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $gnuTar) { throw 'Git for Windows (for GNU tar with xz) is required to build the toolchain archive.' }

$llvmArchive = Get-Pinned $llvm
$llvmDir = Join-Path $staging 'llvm'
New-Item -ItemType Directory -Force -Path $llvmDir | Out-Null
$top = "clang+llvm-$($llvm.Version)-x86_64-pc-windows-msvc"
$env:PATH = (Split-Path -Parent $gnuTar) + ';' + $env:PATH
& $gnuTar --force-local -xJf ($llvmArchive -replace '\\', '/') -C ($llvmDir -replace '\\', '/') --strip-components=1 `
    ($llvmFiles | ForEach-Object { "$top/$_" })
if ($LASTEXITCODE -ne 0) { throw 'Extracting LLVM failed.' }

$cmakeArchive = Get-Pinned $cmake
$cmakeTemp = Join-Path $staging '_cmake'
[IO.Compression.ZipFile]::ExtractToDirectory($cmakeArchive, $cmakeTemp)
Move-Item -LiteralPath (Get-ChildItem -LiteralPath $cmakeTemp -Directory | Select-Object -First 1).FullName `
    -Destination (Join-Path $staging 'cmake')
Remove-Item -LiteralPath $cmakeTemp -Recurse -Force
Remove-Item -LiteralPath (Join-Path $staging 'cmake\doc'), (Join-Path $staging 'cmake\man') -Recurse -Force -ErrorAction SilentlyContinue

$ninjaArchive = Get-Pinned $ninja
[IO.Compression.ZipFile]::ExtractToDirectory($ninjaArchive, (Join-Path $staging 'ninja'))

$licenseDir = Join-Path $staging 'licenses'
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
foreach ($name in $licenses.Keys) {
    & curl.exe -fsSL -o (Join-Path $licenseDir $name) $licenses[$name]
    if ($LASTEXITCODE -ne 0) { throw "License download failed: $name" }
}
Copy-Item -LiteralPath (Join-Path $staging 'cmake\share\cmake-4.4\Copyright.txt') `
    -Destination (Join-Path $licenseDir 'CMake-Copyright.txt') -ErrorAction SilentlyContinue
@"
MKVDCU-Recomp build toolchain $id

Unmodified binaries from the official releases:
  LLVM $($llvm.Version)  $($llvm.Url)
  CMake $($cmake.Version)  $($cmake.Url)
  Ninja $($ninja.Version)  $($ninja.Url)
Licenses are in this folder.
"@ | Set-Content -LiteralPath (Join-Path $licenseDir 'README.txt') -Encoding ASCII

[PSCustomObject]@{ id = $id; llvm = $llvm.Version; cmake = $cmake.Version; ninja = $ninja.Version } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $staging 'toolchain.json') -Encoding ASCII

if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
[IO.Compression.ZipFile]::CreateFromDirectory($staging, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
[PSCustomObject]@{ id = $id; file = (Split-Path -Leaf $zip); sha256 = $hash; size = (Get-Item -LiteralPath $zip).Length }
