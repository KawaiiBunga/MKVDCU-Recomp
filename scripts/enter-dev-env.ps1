<#
.SYNOPSIS
Adds workspace-local ReXGlue, LLVM, and native CMake tools to the current PowerShell session.
#>
[CmdletBinding()]
param()

$root = Split-Path -Parent $PSScriptRoot
$candidates = @(
  (Join-Path $root 'references\rexglue-sdk\out\install\win-amd64\bin'),
  'C:\Program Files\LLVM\bin',
  'C:\Program Files\CMake\bin'
)
foreach ($candidate in $candidates) {
  if (Test-Path -LiteralPath $candidate) { $env:Path = "$candidate;$env:Path" }
}
Write-Host 'Development tools added to this PowerShell session. Try: rexglue --version'
