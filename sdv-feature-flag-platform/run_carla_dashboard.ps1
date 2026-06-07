#Requires -Version 5.1
<#
.SYNOPSIS
  One-click SDV platform + CARLA + pygame bridge + Qt Dashboard.

.DESCRIPTION
  Starts (in order):
    1) SDV full stack — run_sdv.ps1 (car_1..car_10, Docker, status-server, web UIs)
    2) CARLA — start_carla.ps1
    3) SDV fleet — spawn_sdv_fleet.py car_1..car_10 (full stack) or generate_traffic (SingleCar)
    4) Bridge — stop_bridge_ports.ps1 + pygame_map_bridge.py (:9090)
    5) Qt Dashboard — vehicle/MQTT/API + CARLA bridge mode

  Use -SingleCar for a lighter demo (run_sdv2 + one vehicle only).

.EXAMPLE
  .\run_carla_dashboard.ps1
  .\run_carla_dashboard.ps1 -Car 2
  .\run_carla_dashboard.ps1 -SingleCar
  .\run_carla_dashboard.ps1 -SkipSdv
  .\run_carla_dashboard.ps1 -TrafficVehicles 10
  .\run_carla_dashboard.ps1 -SkipTraffic
#>
[CmdletBinding()]
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor",
    [ValidateRange(1, 10)]
    [int]$Car = 1,
    [string]$RosWsUrl = "ws://127.0.0.1:9090",
    [int]$BridgeWaitSec = 60,
    [int]$CarlaWaitSec = 120,
    [int]$CarlaApiVerifySec = 90,
    [switch]$SingleCar,
    [switch]$SkipSdv,
    [switch]$SkipCarla,
    [switch]$SkipTraffic,
    [ValidateRange(0, 200)]
    [int]$TrafficVehicles = 10,
    [switch]$SkipBridge,
    [switch]$SkipDashboard
)

$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$TeamRoot = (Resolve-Path (Join-Path $ProjectRoot "..")).Path
$OpsScripts = Join-Path $TeamRoot "sdv-operations-center\scripts"
$CarId = "car_$Car"

. (Join-Path $ProjectRoot "scripts\SdvRunCommon.ps1")

function Test-TcpPortOpen {
    param([int]$Port)
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $client.Connect("127.0.0.1", $Port)
        $client.Close()
        return $true
    } catch {
        return $false
    }
}

function Wait-TcpPort {
    param(
        [int]$Port,
        [int]$TimeoutSec = 45
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-TcpPortOpen $Port) { return $true }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

function Get-PythonCmd {
    foreach ($name in @("python", "python3", "py")) {
        if (Get-Command $name -ErrorAction SilentlyContinue) {
            if ($name -eq "py") { return "py -3" }
            return $name
        }
    }
    return $null
}

function Read-OptionalMapboxToken {
    if ($env:MAPBOX_TOKEN) { return $env:MAPBOX_TOKEN.Trim() }
    if ($env:MAPBOX_ACCESS_TOKEN) { return $env:MAPBOX_ACCESS_TOKEN.Trim() }
    $tokenFile = Join-Path $ProjectRoot "sdv-dashboard-qt\.mapbox_token"
    if (Test-Path -LiteralPath $tokenFile) {
        $line = (Get-Content -LiteralPath $tokenFile -Raw).Trim()
        if ($line -and -not $line.StartsWith("#")) {
            return $line.Trim('"').Trim("'")
        }
    }
    return ""
}

function Start-CmdWindow {
    param(
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][string]$WorkingDir,
        [Parameter(Mandatory)][string]$Command
    )
    $cmdLine = "title $Title && cd /d `"$WorkingDir`" && $Command"
    Write-Host "[run_carla_dashboard] Opening window: $Title"
    Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmdLine) -WindowStyle Normal
}

if (-not (Test-Path -LiteralPath $OpsScripts)) {
    throw "sdv-operations-center scripts not found: $OpsScripts"
}

Write-Host "=== SDV + CARLA Dashboard (one-click) ===" -ForegroundColor Cyan
Write-Host ""

# --- 0) SDV platform: infra + vehicle + UI servers ---
if (-not $SkipSdv) {
    if ($SingleCar) {
        Write-Host "[0/5] Starting SDV infra + $CarId only (-SingleCar)..."
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot "run_sdv2.ps1")
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "run_sdv2.ps1 exited with code $LASTEXITCODE (continuing)"
        }

        Write-Host "[0/5] Starting vehicle $CarId (WSL)..."
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File `
            (Join-Path $ProjectRoot "scripts\start_car.ps1") -Car $Car -SkipQt
        if ($LASTEXITCODE -ne 0) {
            throw "start_car.ps1 failed for $CarId (exit $LASTEXITCODE)"
        }
        Start-Sleep -Seconds 3
    } else {
        Write-Host "[0/5] Starting full SDV stack (run_sdv.ps1, car_1..car_10)..."
        $env:SDV_SKIP_QT_DASHBOARD = "1"
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot "run_sdv.ps1")
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "run_sdv.ps1 exited with code $LASTEXITCODE (continuing with CARLA stack)"
        }
    }

    if (-not (Wait-TcpPort -Port 8080 -TimeoutSec 45)) {
        Write-Warning "status-server :8080 not ready — Dashboard vehicle API may be offline."
    } else {
        Write-Host "[0/5] status-server ready on :8080"
    }
} else {
    Write-Host "[0/5] Skipped SDV stack (SkipSdv) — ensure run_sdv / run_sdv2 + car_* are already running."
}

# --- 1) CARLA ---
if (-not $SkipCarla) {
    Write-Host "[1/5] Starting CARLA (start_carla.ps1)..."
    $startCarla = Join-Path $OpsScripts "start_carla.ps1"
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $startCarla `
        -CarlaRoot $CarlaRoot -WaitSec $CarlaWaitSec -ApiVerifySec $CarlaApiVerifySec
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "start_carla.ps1 failed (exit $LASTEXITCODE). Try: sdv-operations-center\scripts\stop_carla.ps1 then re-run."
        throw "start_carla.ps1 failed (exit $LASTEXITCODE)"
    }
} else {
    Write-Host "[1/5] Skipped CARLA (SkipCarla)"
    if (-not (Test-TcpPortOpen 2000)) {
        Write-Warning "CARLA RPC :2000 is not open — bridge may fail until CARLA starts."
    }
}

# --- 2) CARLA traffic / SDV fleet ---
if (-not $SkipTraffic) {
    if (-not (Test-TcpPortOpen 2000)) {
        Write-Warning "[2/5] CARLA RPC :2000 not open — skipping fleet/traffic spawn"
    } elseif ($SingleCar) {
        Write-Host "[2/5] Spawning NPC traffic (generate_traffic.py -n $TrafficVehicles)..."
        $spawnTraffic = Join-Path $OpsScripts "spawn_carla_traffic.ps1"
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $spawnTraffic `
            -CarlaRoot $CarlaRoot -VehicleCount $TrafficVehicles
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "spawn_carla_traffic.ps1 failed (exit $LASTEXITCODE) — continuing."
        }
    } else {
        Write-Host "[2/5] Spawning SDV fleet in CARLA (car_1..car_10 = WSL fleet)..."
        $spawnFleet = Join-Path $OpsScripts "spawn_sdv_fleet.ps1"
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $spawnFleet `
            -CarlaRoot $CarlaRoot -FleetCount 10
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "spawn_sdv_fleet.ps1 failed (exit $LASTEXITCODE) — continuing with hero-only sim."
        }
    }
} else {
    Write-Host "[2/5] Skipped fleet/traffic (SkipTraffic)"
}

# --- 3) pygame map bridge ---
if (-not $SkipBridge) {
    Write-Host "[3/5] Starting pygame map bridge (:9090)..."
    $bridgeScript = Join-Path $OpsScripts "pygame_map_bridge.py"
    if (-not (Test-Path -LiteralPath $bridgeScript)) {
        throw "Missing: $bridgeScript"
    }

    if (Test-TcpPortOpen 9090) {
        Write-Host "[3/5] Port 9090 already in use — assuming bridge is running."
    } else {
        $py = Get-PythonCmd
        if (-not $py) {
            throw "Python not found. Install Python 3 or add it to PATH."
        }

        $bridgeCmd = "powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\stop_bridge_ports.ps1 && $py .\pygame_map_bridge.py"
        Start-CmdWindow -Title "pygame map bridge :9090" -WorkingDir $OpsScripts -Command $bridgeCmd

        if (-not (Wait-TcpPort -Port 9090 -TimeoutSec $BridgeWaitSec)) {
            Write-Warning "Bridge did not open :9090 within ${BridgeWaitSec}s — check the bridge window."
        } else {
            Write-Host "[3/5] Bridge listening on ws://127.0.0.1:9090"
        }
    }
} else {
    Write-Host "[3/5] Skipped bridge (SkipBridge)"
}

# --- 4) Qt SDV Dashboard (vehicle + CARLA) ---
if (-not $SkipDashboard) {
    Write-Host "[4/5] Launching SDV_Dashboard ($CarId + CARLA bridge)..."
    $mapboxToken = Read-OptionalMapboxToken
    $launched = Start-SdvQtDashboard `
        -ProjectRoot $ProjectRoot `
        -CarId $CarId `
        -LogPrefix "run_carla_dashboard" `
        -RosWsUrl $RosWsUrl `
        -CarlaMode "bridge" `
        -MapboxToken $mapboxToken `
        -ForceRestart
    if (-not $launched) {
        exit 1
    }
} else {
    Write-Host "[4/5] Skipped dashboard (SkipDashboard)"
}

$vehicleLine = if ($SingleCar -or $SkipSdv) { "$CarId (WSL)" } else { "car_1..car_10 (WSL)" }

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  Vehicles      : $vehicleLine + MQTT :1884"
Write-Host "  Dashboard car : $CarId"
Write-Host "  API / UI      : http://127.0.0.1:8080  admin-portal: http://127.0.0.1:5170"
Write-Host "  CARLA RPC     : 127.0.0.1:2000"
if (-not $SkipTraffic) {
    if ($SingleCar) {
        Write-Host "  NPC traffic   : generate_traffic.py -n $TrafficVehicles"
    } else {
        Write-Host "  CARLA fleet   : spawn_sdv_fleet.py car_1..car_10 (hero + autopilot)"
    }
}
Write-Host "  Map bridge WS : $RosWsUrl"
Write-Host ""
