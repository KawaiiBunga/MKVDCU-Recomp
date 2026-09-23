[CmdletBinding()]
param([ValidateSet('pc','switch')][string]$Mode = 'pc')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Get-Command python -ErrorAction Stop).Source (Join-Path $PSScriptRoot 'doctor.py') --mode $Mode
exit $LASTEXITCODE
