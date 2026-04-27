#Requires -Version 5.1
$ErrorActionPreference = "Stop"

# Same folder as run_sdv.bat (Unicode-safe paths; avoids cmd.exe FOR /F + UTF-8 mojibake).
$ProjectRoot = $PSScriptRoot
$TeamRoot = (Resolve-Path (Join-Path $ProjectRoot "..")).Path

function ConvertTo-WslPath {
    param([Parameter(Mandatory)][string]$WindowsPath)
    $resolved = (Resolve-Path -LiteralPath $WindowsPath).Path
    # Avoid calling `wslpath` from Windows here: on some systems it mangles backslashes
    # (e.g. "C:\Users\..." -> "C:Users...") and breaks non-ASCII paths.
    #
    # This repo lives on a local drive, so a deterministic conversion is fine:
    #   C:\foo\bar -> /mnt/c/foo/bar
    if ($resolved -notmatch '^[A-Za-z]:\\') {
        throw "Unsupported path for WSL conversion: $resolved"
    }
    $drive = $resolved.Substring(0, 1).ToLowerInvariant()
    $rest = $resolved.Substring(3) -replace '\\', '/'
    return "/mnt/$drive/$rest"
}

$projWsl = ConvertTo-WslPath $ProjectRoot
$teamWsl = ConvertTo-WslPath $TeamRoot

$buildUnleashMarker = Join-Path $ProjectRoot "build_unleash"
$buildUnleashMarker = Join-Path $buildUnleashMarker "SdvPlatform"
$buildWsl = if (Test-Path -LiteralPath $buildUnleashMarker) { "$projWsl/build_unleash" } else { "$projWsl/build" }

Write-Host "Cleaning up previous processes..."
$null = & wsl.exe -d Ubuntu -u root -- bash -c "pkill -f SdvPlatform; pkill -f 'car_[0-9]'; pkill -f EventBusHost; pkill -f WatchDog; pkill -f FeatureEvaluator; pkill -f DataCollector; pkill -f SignalReceiver; pkill -f SignalSender" 2>$null
Start-Sleep -Seconds 2

Write-Host "Ensuring no manual mosquitto is running in WSL (Docker broker mode)..."
$null = & wsl.exe -d Ubuntu -u root -- bash -c "pkill -x mosquitto || true" 2>$null
Start-Sleep -Milliseconds 600

Write-Host "Starting docker stack (teamproject/docker-compose.yml)..."
& wsl.exe -d Ubuntu -- bash "$projWsl/scripts/docker_up_team_wsl.sh" "$teamWsl"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "docker compose exited with code $LASTEXITCODE (continuing)"
}

Write-Host "Waiting for Unleash (http://localhost:4242)..."
& wsl.exe -d Ubuntu -- bash "$projWsl/scripts/wait_unleash_http.sh"

$append = @()
if ($env:UNLEASH_URL) { $append += "UNLEASH_URL" }
if ($env:UNLEASH_API_KEY) { $append += "UNLEASH_API_KEY" }
if ($env:UNLEASH_APP_NAME) { $append += "UNLEASH_APP_NAME" }
if ($append.Count -gt 0) {
    $piece = $append -join ":"
    if ($env:WSLENV) {
        $env:WSLENV = "$($env:WSLENV):$piece"
    } else {
        $env:WSLENV = $piece
    }
}

Write-Host "Starting SDV platform (WSL)..."
$carConfs = 1..10 | ForEach-Object { "config/car/car_$_.conf" }
foreach ($conf in $carConfs) {
    Write-Host "Starting SDV platform (WSL) with $conf..."
    Start-Process -FilePath "wsl.exe" -ArgumentList @(
        "-d", "Ubuntu",
        "-u", "root",
        "--", "bash", "$projWsl/scripts/start_platform_wsl.sh", $projWsl, $buildWsl, $conf
    ) -WindowStyle Minimized
    Start-Sleep -Milliseconds 150
}

function Stop-ListenPorts {
    param([int[]]$Ports)
    foreach ($port in $Ports) {
        try {
            $owners = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty OwningProcess -Unique
            foreach ($procId in $owners) {
                if ($procId -and $procId -gt 0) {
                    Write-Host "[run_sdv] Port $port in use by PID $procId - stopping so Vite can bind (strictPort)."
                    Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
                }
            }
        } catch {
            # Older Windows / restricted environments may not expose Get-NetTCPConnection.
        }
    }
}

function Start-OptionalCmdWindow {
    param([string]$SubDir, [string]$NpmScript = "dev")
    $dir = Join-Path $ProjectRoot $SubDir
    $pkg = Join-Path $dir "package.json"
    if (-not (Test-Path -LiteralPath $pkg)) { return }
    $cmd = "cd /d `"$dir`" && npm install && npm run $NpmScript"
    Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmd) -WindowStyle Normal
}

# status-server=8080, dashboard=5173, test-ui=5174. Prior runs often leave node.exe listening.
Stop-ListenPorts @(8080, 5173, 5174)
Start-Sleep -Milliseconds 800

Start-OptionalCmdWindow "status-server" "start"
Start-OptionalCmdWindow "dashboard"
Start-OptionalCmdWindow "test-ui"

Write-Host "Done. test-ui: http://127.0.0.1:5174 (if test-ui package exists)"
