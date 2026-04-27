#Requires -Version 5.1
$ErrorActionPreference = "Stop"

# Same folder as run_sdv.bat (Unicode-safe paths; avoids cmd.exe FOR /F + UTF-8 mojibake).
$ProjectRoot = $PSScriptRoot
$TeamRoot = (Resolve-Path (Join-Path $ProjectRoot "..")).Path

function Resolve-WslDistro {
    # Allow explicit override (team-friendly, avoids hardcoding Ubuntu name).
    if ($env:SDV_WSL_DISTRO -and $env:SDV_WSL_DISTRO.Trim().Length -gt 0) {
        return $env:SDV_WSL_DISTRO.Trim()
    }

    # Prefer the historical default used by this repo if it exists.
    try {
        $distros = & wsl.exe -l -q 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $distros) { throw "wsl list failed" }
        $distros = $distros | ForEach-Object { $_.Trim() } | Where-Object { $_ }
        if ($distros -contains "Ubuntu") { return "Ubuntu" }
        # Fallback: pick the first Ubuntu* distro if present, else first distro.
        $ubuntuLike = $distros | Where-Object { $_ -like "Ubuntu*" } | Select-Object -First 1
        if ($ubuntuLike) { return $ubuntuLike }
        return ($distros | Select-Object -First 1)
    } catch {
        # Final fallback: let wsl.exe decide default distro (no -d flag).
        return ""
    }
}

$WslDistro = Resolve-WslDistro
if ($WslDistro) {
    Write-Host "Using WSL distro: $WslDistro (override with `$env:SDV_WSL_DISTRO)"
} else {
    Write-Host "Using WSL default distro (override with `$env:SDV_WSL_DISTRO)"
}

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
$wslArgsBase = @()
if ($WslDistro) { $wslArgsBase += @("-d", $WslDistro) }
$wslArgsRoot = $wslArgsBase + @("-u", "root", "--")

$null = & wsl.exe @wslArgsRoot bash -c "pkill -f SdvPlatform; pkill -f 'car_[0-9]'; pkill -f EventBusHost; pkill -f WatchDog; pkill -f FeatureEvaluator; pkill -f DataCollector; pkill -f SignalReceiver; pkill -f SignalSender" 2>$null
Start-Sleep -Seconds 2
# Wait until old processes are fully gone; prevents LifeCycleMgr from seeing "stale PIDs" mid-shutdown.
& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/wait_shutdown_wsl.sh`" 2>/dev/null || true"
& wsl.exe @wslArgsRoot bash "$projWsl/scripts/wait_shutdown_wsl.sh" "30" "0.3"

Write-Host "Ensuring no manual mosquitto is running in WSL (Docker broker mode)..."
$null = & wsl.exe @wslArgsRoot bash -c "pkill -x mosquitto || true" 2>$null
Start-Sleep -Milliseconds 600

Write-Host "Starting docker stack (teamproject/docker-compose.yml)..."
& wsl.exe @wslArgsBase -- bash "$projWsl/scripts/docker_up_team_wsl.sh" "$teamWsl"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "docker compose exited with code $LASTEXITCODE (continuing)"
}

Write-Host "Waiting for Unleash (http://localhost:4242)..."
& wsl.exe @wslArgsBase -- bash "$projWsl/scripts/wait_unleash_http.sh"

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
$logRel = "logs/run_sdv"
$logWsl = "$projWsl/$logRel"
$logWin = Join-Path $ProjectRoot $logRel
$null = New-Item -ItemType Directory -Force -Path $logWin 2>$null
& wsl.exe @wslArgsRoot bash -lc "mkdir -p `"$logWsl`""

Write-Host "Logs will be written under: $logWin"
# Windows checkouts can introduce CRLF into bash scripts which breaks `set -o pipefail`.
# Normalize a small set of WSL scripts on each run (portable: uses sed, not dos2unix).
& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/run_cars_wsl.sh`" `"$projWsl/scripts/start_platform_wsl.sh`" 2>/dev/null || true"
& wsl.exe @wslArgsRoot bash "$projWsl/scripts/run_cars_wsl.sh" "$projWsl" "$buildWsl" "$logWsl" "1" "10"

# Make it "visibly running": open log-follow consoles (one per car).
# This avoids spawning 10 WSL shells running the binaries directly (less fragile), while still giving
# developers an immediate live view like the old single-car console flow.
Write-Host "Opening car log consoles (car_1..car_10)..."
foreach ($i in 1..10) {
    $carName = "car_$i"
    $logFileWsl = "$logWsl/$carName.log"
    # Do not pass `-d` here: distro names differ across machines (Ubuntu vs Ubuntu-22.04, etc).
    # Use the user's default WSL distro to avoid WSL_E_DISTRO_NOT_FOUND.
    $cmd = "wsl.exe -u root -- bash -lc `"tail -n +1 -f '$logFileWsl'`""
    Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmd) -WindowStyle Normal
    Start-Sleep -Milliseconds 120
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
