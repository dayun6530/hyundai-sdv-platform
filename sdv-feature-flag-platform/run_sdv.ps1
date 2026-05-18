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
    # status-server / Node children must match (execWslBash uses SDV_WSL_DISTRO before defaulting to Ubuntu).
    $env:SDV_WSL_DISTRO = $WslDistro
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
$buildRel = if (Test-Path -LiteralPath $buildUnleashMarker) { "build_unleash" } else { "build" }

Write-Host "Cleaning up previous SDV / car processes in WSL (full restart)..."
$wslArgsBase = @()
if ($WslDistro) { $wslArgsBase += @("-d", $WslDistro) }
$wslArgsRoot = $wslArgsBase + @("-u", "root", "--")

# Root pkill: any leftover car_1..car_10 or platform children from an earlier run_sdv / manual start.
# Kill car_10..car_1 in that order so "car_1" does not match a still-running ".../car_10" cmdline.
$cleanupBash = @'
set +e
pkill -9 -f SdvPlatform 2>/dev/null
for i in $(seq 10 -1 1); do
  pkill -9 -f "/build_unleash/car_${i}([[:space:]]|$)" 2>/dev/null
  pkill -9 -f "/build/car_${i}([[:space:]]|$)" 2>/dev/null
done
pkill -9 -f 'sdv-feature-flag-platform/build_unleash/car_' 2>/dev/null
pkill -9 -f 'sdv-feature-flag-platform/build/car_' 2>/dev/null
pkill -9 -f EventBusHost 2>/dev/null
pkill -9 -f WatchDog 2>/dev/null
pkill -9 -f FeatureEvaluator 2>/dev/null
pkill -9 -f DataCollector 2>/dev/null
pkill -9 -f SignalReceiver 2>/dev/null
pkill -9 -f SignalSender 2>/dev/null
pkill -9 -f MqttVehicleAgentHost 2>/dev/null
pkill -9 -f NavigationFeature 2>/dev/null
pkill -9 -f AdasFeature 2>/dev/null
pkill -9 -f EvEnergyFeature 2>/dev/null
pkill -9 -f DeploymentMgr 2>/dev/null
exit 0
'@
$null = & wsl.exe @wslArgsRoot bash -c $cleanupBash 2>$null
Start-Sleep -Seconds 3

# Wait until old processes are fully gone; avoids two car_N parents interleaving in the same log tail.
& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/wait_shutdown_wsl.sh`" 2>/dev/null || true"
& wsl.exe @wslArgsRoot bash "$projWsl/scripts/wait_shutdown_wsl.sh" "60" "0.3"

Write-Host "Ensuring no manual mosquitto is running in WSL (Docker broker mode)..."
$null = & wsl.exe @wslArgsRoot bash -c "pkill -x mosquitto || true" 2>$null
Start-Sleep -Milliseconds 600

# Strip CRLF before any bash scripts run (fixes `set: pipefail: invalid option name` on Windows checkouts).
& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/docker_up_team_wsl.sh`" `"$projWsl/scripts/wait_unleash_http.sh`" `"$projWsl/scripts/wait_shutdown_wsl.sh`" 2>/dev/null || true"

Write-Host "Starting docker stack (teamproject/docker-compose.yml: mosquitto, loki, grafana, unleash, operational-data-api, pgadmin, ...)..."
& wsl.exe @wslArgsBase -- bash "$projWsl/scripts/docker_up_team_wsl.sh" "$teamWsl"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "docker compose exited with code $LASTEXITCODE (continuing)"
} else {
    Write-Host "Docker: pgAdmin http://127.0.0.1:5050  (admin@admin.com / admin) — connects to operational-postgres on the compose network"
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
# Normalize remaining WSL scripts (run_cars / start_platform / per-car foreground).
& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/run_cars_wsl.sh`" `"$projWsl/scripts/start_platform_wsl.sh`" `"$projWsl/scripts/run_car_foreground_wsl.sh`" 2>/dev/null || true"

# Optional incremental build (off by default). After C++ changes, run once with SDV_CMAKE_BUILD=1 (rebuilds all car_* + libs).
if ($env:SDV_CMAKE_BUILD -eq "1") {
    Write-Host "Incremental cmake ($buildRel): all targets (SDV_CMAKE_BUILD=1)..."
    & wsl.exe @wslArgsBase bash -lc "cd `"$projWsl`" && cmake --build `"$buildRel`" -j 4"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "cmake --build failed (exit $LASTEXITCODE); continuing with existing binaries."
    }
}



# Optional fleet telemetry sink (Linux binary). Each car config already launches DataCollector per vehicle —
# use SDV_STANDALONE_DATACOLLECTOR=1 only if you need an extra host-side sink against EventBus 50051.
if ($env:SDV_STANDALONE_DATACOLLECTOR -eq "1") {
    $collectorCmd = "mkdir -p '$projWsl/logs/fleet' && cd '$projWsl' && export SDV_LOG_DIR='$projWsl/logs/fleet' && exec '$buildWsl/DataCollector' 127.0.0.1:50051"
    Write-Host "[run_sdv] Starting standalone DataCollector (SDV_STANDALONE_DATACOLLECTOR=1)..."
    $wslDcArgs = $wslArgsRoot + @("bash", "-lc", $collectorCmd)
    Start-Process wsl.exe -ArgumentList $wslDcArgs -WindowStyle Minimized
}


# Default: one CMD window per car (title car_N), each running WSL foreground car_N (Linux binary under
# build_unleash or build — same as old ".\build\car_N in a CMD" workflow visually).
# Set SDV_CAR_BACKGROUND=1 for no popups: background start in WSL + tail -f log windows instead.
if ($env:SDV_CAR_BACKGROUND -eq "1") {
    Write-Host "SDV_CAR_BACKGROUND=1: starting cars in WSL background + log tail CMD windows..."
    & wsl.exe @wslArgsRoot bash "$projWsl/scripts/run_cars_wsl.sh" "$projWsl" "$buildWsl" "$logWsl" "1" "10"

    Write-Host "Opening car log consoles (car_1..car_10)..."
    foreach ($i in 1..10) {
        $carName = "car_$i"
        $logFileWsl = "$logWsl/$carName.log"
        $cmd = "wsl.exe -u root -- bash -lc `"tail -n +1 -f '$logFileWsl'`""
        Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmd) -WindowStyle Normal
        Start-Sleep -Milliseconds 120
    }
} else {
    # via start_platform_wsl.sh so SDV_MQTT_BROKER_URI and paths match the scripted start path.
    # Do NOT pass `-d <distro>` here: names from `wsl -l -q` often differ from what `wsl -d` accepts
    # on some PCs (WSL_E_DISTRO_NOT_FOUND). Use the default distro, same as the old tail -f consoles.
    Write-Host "Opening car consoles (car_1..car_10): $buildRel/car_N + config/car/car_N.conf"
    foreach ($i in 1..10) {
        $carConf = "config/car/car_$i.conf"
        $bashLine = "cd '$projWsl' && bash '$projWsl/scripts/start_platform_wsl.sh' '$projWsl' '$buildWsl' '$carConf' --foreground"
        $wslCmd = "title car_$i && wsl.exe -u root -- bash -lc `"$bashLine`""
        Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $wslCmd) -WindowStyle Normal
        Start-Sleep -Milliseconds 500
    }
}

# Small verification in the same terminal (helps confirm "10대 다 켜짐"); allow last car to attach.
Start-Sleep -Seconds 3
& wsl.exe @wslArgsBase -- bash -lc "cd $projWsl; echo '---- running cars ----'; ps aux | grep -E 'build_unleash/car_[0-9]+|build/car_[0-9]+' | grep -v grep || true; echo '---- EventBusHost ports ----'; ss -lntp | grep -E ':5005[0-9]' || true"

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

# status-server=8080, dashboard=5173, test-ui=5174, sdv-cockpit-ui=5175
Stop-ListenPorts @(8080, 5173, 5174, 5175)
Start-Sleep -Milliseconds 800

$viteDiag = Join-Path $ProjectRoot "scripts\diagnose_vite_ports.ps1"
if (Test-Path -LiteralPath $viteDiag) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $viteDiag -Quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Vite ports 5173-5175 may be blocked on this Windows host. Run: .\scripts\diagnose_vite_ports.ps1 -FixHint"
    }
}

Start-OptionalCmdWindow "status-server" "start"
Start-OptionalCmdWindow "dashboard"
Start-OptionalCmdWindow "test-ui"
Start-OptionalCmdWindow "sdv-cockpit-ui"

Write-Host "Waiting for status-server to drain pending Docker OTA queue (if any)..."
$healthOk = $false
for ($i = 0; $i -lt 40; $i++) {
    try {
        $h = Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/vehicle-runtime/health" -UseBasicParsing -TimeoutSec 2 -ErrorAction Stop
        if ($h.StatusCode -eq 200) {
            $healthOk = $true
            break
        }
    } catch {
        # server not listening yet
    }
    Start-Sleep -Milliseconds 500
}
if ($healthOk) {
    try {
        $resp = Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/vehicle-runtime/apply-pending" -Method POST -UseBasicParsing -TimeoutSec 300 -ErrorAction Stop
        Write-Host "[run_sdv] apply-pending: $($resp.Content)"
    } catch {
        Write-Warning "[run_sdv] apply-pending skipped or failed: $_"
    }
} else {
    Write-Warning "[run_sdv] status-server not ready on :8080; skipped apply-pending (start status-server manually if needed)"
}

Write-Host "Done. dashboard: http://127.0.0.1:5173  test-ui: http://127.0.0.1:5174  cockpit-ui: http://127.0.0.1:5175  pgAdmin: http://127.0.0.1:5050"

# --- Qt SDV Dashboard (Windows native UI; subscribe-only over HTTP + optional MQTT) ---
if ($env:SDV_SKIP_QT_DASHBOARD -eq "1") {
    Write-Host "[run_sdv] SDV_SKIP_QT_DASHBOARD=1 — skipping native Qt dashboard launch."
} else {
    $dashRoot = Join-Path $ProjectRoot "sdv-dashboard-qt\build"
    $dashCandidates = @(
        (Join-Path $dashRoot "Release\SDV_Dashboard.exe"),
        (Join-Path $dashRoot "RelWithDebInfo\SDV_Dashboard.exe"),
        (Join-Path $dashRoot "Debug\SDV_Dashboard.exe"),
        (Join-Path $dashRoot "SDV_Dashboard.exe")
    )
    $dashExe = $dashCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($dashExe) {
        Write-Host "[run_sdv] Launching Qt SDV dashboard: $dashExe"
        Start-Process -FilePath $dashExe -ArgumentList @(
            "--api-base", "http://127.0.0.1:8080",
            "--mqtt-host", "127.0.0.1",
            "--mqtt-port", "1884",
            "--car-id", "car_1"
        ) -WorkingDirectory (Split-Path -Parent $dashExe)
    } else {
        Write-Warning "[run_sdv] SDV_Dashboard.exe not found. Build once on Windows: cmake -S sdv-dashboard-qt -B sdv-dashboard-qt/build -DCMAKE_PREFIX_PATH=<Qt6_DIR> && cmake --build sdv-dashboard-qt/build --config Release"
    }
}