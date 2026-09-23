[CmdletBinding()]
param(
    [ValidateRange(1, 500)]
    [int]$Iterations = 25,

    [ValidateRange(5, 1800)]
    [int]$HostRunSeconds = 600,

    [string]$GameDataRoot = ''
)

$ErrorActionPreference = 'Stop'

# The script is shareable; its function seed file and logs remain local. It
# learns only targets observed by a real host execution.
$WorkspaceRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Join-Path $WorkspaceRoot 'targets\mkvsdcu\private\rexglue-host'
if (-not $GameDataRoot) { $GameDataRoot = Join-Path $WorkspaceRoot 'user-game-files\work\mkvsdcu' }
$ConfigPath = Join-Path $ProjectRoot 'config\mkvsdcu_functions.toml'
$ManifestPath = Join-Path $ProjectRoot 'mkvsdcu_manifest.toml'
$BuildPath = Join-Path $ProjectRoot 'out\build\win-amd64-release'
$HostExe = Join-Path $BuildPath 'mkvsdcu.exe'
$ReXGlue = Join-Path $WorkspaceRoot 'references\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe'
$CMake = if (Test-Path -LiteralPath 'C:\Program Files\CMake\bin\cmake.exe') {
    'C:\Program Files\CMake\bin\cmake.exe'
} else {
    (Get-Command cmake -ErrorAction Stop).Source
}
$RuntimeUser = Join-Path $ProjectRoot 'runtime-user'
$RuntimeCache = Join-Path $ProjectRoot 'runtime-cache'
$RunId = Get-Date -Format 'yyyyMMdd-HHmmss'
$ControllerLog = Join-Path $BuildPath "indirect-dispatch-controller-$RunId.log"

foreach ($required in @($GameDataRoot, $ConfigPath, $ManifestPath, $ReXGlue, $CMake)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path does not exist: $required"
    }
}
$gameXex = Join-Path $GameDataRoot 'default.xex'
if (-not (Test-Path -LiteralPath $gameXex -PathType Leaf)) { throw "Required game executable does not exist: $gameXex" }
$expectedXexHash = '2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7'
$actualXexHash = (Get-FileHash -LiteralPath $gameXex -Algorithm SHA256).Hash
if ($actualXexHash -ne $expectedXexHash) {
    throw "Game XEX SHA-256 $actualXexHash does not match the function config's $expectedXexHash."
}

New-Item -ItemType Directory -Force -Path $RuntimeUser, $RuntimeCache | Out-Null

function Write-ControllerLog([string]$Message) {
    $line = "[{0:yyyy-MM-dd HH:mm:ss}] {1}" -f (Get-Date), $Message
    Add-Content -LiteralPath $ControllerLog -Value $line -Encoding utf8
    Write-Host $line
}

function Wait-ForCodegen {
    # The CLI can leave a child worker alive after an interrupted shell; never overlap it.
    while ($existing = @(Get-Process rexglue -ErrorAction SilentlyContinue)) {
        Write-ControllerLog "Waiting for existing codegen worker(s): $($existing.Id -join ', ')"
        Start-Sleep -Seconds 5
    }
}

function Add-VerifiedTarget([string]$Address) {
    $pattern = '(?m)^"0x' + [regex]::Escape($Address) + '"\s*=\s*\{\}\s*$'
    $contents = [System.IO.File]::ReadAllText($ConfigPath)
    if ($contents -match $pattern) {
        return $false
    }
    [System.IO.File]::AppendAllText($ConfigPath, "`r`n`"0x$Address`" = {}`r`n")
    return $true
}

for ($attempt = 1; $attempt -le $Iterations; ++$attempt) {
    Wait-ForCodegen
    Write-ControllerLog "Iteration $attempt/${Iterations}: codegen"
    & $ReXGlue codegen $ManifestPath
    if ($LASTEXITCODE -ne 0) { throw "ReXGlue codegen failed with exit code $LASTEXITCODE" }

    Write-ControllerLog "Iteration $attempt/${Iterations}: build"
    & $CMake --build $BuildPath --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Host build failed with exit code $LASTEXITCODE" }

    $RunLog = Join-Path $BuildPath ("offline-gpu-windowed-$RunId-{0:D3}.log" -f $attempt)
    $args = '--no-audio_mute --gpu_plugin xenos --game_data_root "{0}" --user_data_root "{1}" --cache_root "{2}" --log_level info --log_file "{3}"' -f $GameDataRoot, $RuntimeUser, $RuntimeCache, $RunLog
    Write-ControllerLog "Iteration $attempt/${Iterations}: launch"
    $process = Start-Process -FilePath $HostExe -ArgumentList $args -PassThru

    $deadline = (Get-Date).AddSeconds($HostRunSeconds)
    do {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    } while (-not $process.HasExited -and (Get-Date) -lt $deadline)

    if (-not $process.HasExited) {
        Write-ControllerLog "Host remains alive after $HostRunSeconds seconds; leaving it running for menu verification. Log: $RunLog"
        exit 0
    }

    if (-not (Test-Path -LiteralPath $RunLog)) {
        throw "Host exited without producing a log: $RunLog"
    }
    $fatal = Select-String -LiteralPath $RunLog -Pattern 'Call to invalid or unregistered function at guest address 0x([0-9A-Fa-f]{8})' | Select-Object -Last 1
    if (-not $fatal) {
        $cleanClose = Select-String -LiteralPath $RunLog -Pattern 'Window closing, shutting down' -Quiet
        $executionComplete = Select-String -LiteralPath $RunLog -Pattern 'Execution complete' -Quiet
        if ($cleanClose -and $executionComplete) {
            Write-ControllerLog "Host window closed cleanly after play; no indirect-dispatch fatal. Log: $RunLog"
            exit 0
        }
        throw "Host exited without an indirect-dispatch fatal; inspect $RunLog"
    }
    $address = $fatal.Matches[0].Groups[1].Value.ToUpperInvariant()
    if (-not (Add-VerifiedTarget $address)) {
        throw "Host repeated already-configured target 0x$address; inspect $RunLog"
    }
    Write-ControllerLog "Recorded verified target 0x$address from $RunLog"
}

Write-ControllerLog "Reached configured iteration limit ($Iterations) without a host that stayed alive."
exit 2
