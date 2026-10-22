param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)][string]$Target)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Junction -Path $Path -Target $Target | Out-Null
