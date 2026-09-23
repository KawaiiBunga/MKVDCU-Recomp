[CmdletBinding()]
param(
    # Label for this run; the CSV is copied to <OutDir>\<Label>.csv
    [Parameter(Mandatory)][string]$Label,
    [ValidateRange(20, 3600)][int]$Seconds = 150,
    # Extra game flags separated by spaces, e.g. '--no-vsync --port_gpu_backend=vulkan'.
    # One string, because powershell -File cannot pass arrays.
    [string]$GameArgs = '',
    [string]$OutDir = '',
    [string]$GameDataRoot = '',
    # Summarise only the last N seconds (steady state after loading).
    [ValidateRange(5, 3600)][int]$Tail = 60
)

# Runs the locally built game unattended with the per-second performance CSV
# enabled, then prints a summary. Compare runs of the same scene (the default
# is launch -> intros -> title screen) with different flags.
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $workspaceRoot 'targets\mkvsdcu\private\rexglue-host\out\build\win-amd64-release'
$exe = Join-Path $buildDir 'mkvsdcu.exe'
if (-not $GameDataRoot) { $GameDataRoot = Join-Path $workspaceRoot 'user-game-files\work\mkvsdcu' }
if (-not $OutDir) { $OutDir = Join-Path $buildDir 'bench' }
$user = Join-Path $OutDir 'user'
$cache = Join-Path $OutDir 'cache'
New-Item -ItemType Directory -Force -Path $user, $cache | Out-Null
$logs = Join-Path $user 'logs'
$before = @(Get-ChildItem $logs -Filter 'perf-*.csv' -ErrorAction SilentlyContinue | % FullName)

$arguments = @('--no-fullscreen', '--audio_mute', '--port_perf_csv', '--no-port_perf_overlay',
    '--gpu_plugin', 'xenos', '--game_data_root', "`"$GameDataRoot`"", '--user_data_root', "`"$user`"",
    '--cache_root', "`"$cache`"", '--log_file', "`"$(Join-Path $OutDir "$Label.log")`"") +
    @($GameArgs -split '[\s,]+' | Where-Object { $_ })
$process = Start-Process $exe -ArgumentList $arguments -PassThru
Start-Sleep -Seconds $Seconds
if ($process.HasExited) { throw "Game exited early (code $($process.ExitCode)); see $(Join-Path $OutDir "$Label.log")" }
Stop-Process -Id $process.Id -Force

$csv = Get-ChildItem $logs -Filter 'perf-*.csv' | Where-Object { $before -notcontains $_.FullName } |
    Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $csv) { throw 'No performance CSV was written.' }
$target = Join-Path $OutDir "$Label.csv"
Copy-Item $csv.FullName $target -Force

# Summarise the steady state at the end of the run.
$rows = @(Import-Csv $target)
$half = $rows | Select-Object -Last $Tail
function Avg($name) { ($half | Measure-Object -Property $name -Average).Average }
function Max($name) { ($half | Measure-Object -Property $name -Maximum).Maximum }
[PSCustomObject]@{
    run = $Label
    seconds = $half.Count
    fps = [math]::Round((Avg 'game_fps'), 2)
    p99_ms = [math]::Round((Avg 'frame_p99_ms'), 2)
    worst_ms = [math]::Round((Max 'frame_max_ms'), 2)
    jitter_ms = [math]::Round((Avg 'frame_stddev_ms'), 3)
    hitches = [int](($half | Measure-Object -Property hitches -Sum).Sum)
    cpu_pct = [math]::Round((Avg 'process_cpu_pct'), 1)
    top_thread_pct = [math]::Round((Avg 'busiest_thread_core_pct'), 1)
    gpu_pct = [math]::Round((Avg 'gpu_3d_pct'), 1)
    vblank_hz = [math]::Round((Avg 'vblank_hz'), 2)
} | Format-List
