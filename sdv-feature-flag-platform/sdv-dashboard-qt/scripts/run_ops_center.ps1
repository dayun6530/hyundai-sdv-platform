#Requires -Version 5.1
<#
  One-shot launcher: status-server (8080) + mock ROS WS (9090) + SDV_Dashboard (Qt).
  Usage:
    .\scripts\run_ops_center.ps1
    $env:MAPBOX_TOKEN = "pk...."; .\scripts\run_ops_center.ps1
    .\scripts\run_ops_center.ps1 -CarId car_2
#>
param(
    [string]$CarId = "car_1",
    [string]$ApiBase = "http://127.0.0.1:8080",
    [string]$RosWsUrl = "ws://127.0.0.1:9090",
    [string]$MqttHost = "127.0.0.1",
    [int]$MqttPort = 1884,
    [string]$QtBin = "C:\Qt\6.8.3\msvc2022_64\bin",
    [string]$Config = "Release",
    [switch]$SkipDeploy,
    [switch]$NoMockWs,
    [switch]$NoStatusServer,
    [switch]$Rebuild,
    [switch]$Carla,
    [ValidateSet("off", "sim", "carla-ws", "bridge")]
    [string]$CarlaMode = "bridge"
)

$ErrorActionPreference = "Stop"

$DashRoot = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent $DashRoot
$TeamRoot = Split-Path -Parent $RepoRoot
$BuildDir = Join-Path $DashRoot "build\$Config"
$Exe = Join-Path $BuildDir "SDV_Dashboard.exe"
$StatusServerDir = Join-Path $RepoRoot "status-server"
$MockWsScript = Join-Path $TeamRoot "sdv-operations-center\scripts\mock_telemetry_ws_server.py"
$CarlaBridgeScript = Join-Path $TeamRoot "sdv-operations-center\scripts\pygame_map_bridge.py"

function Test-TcpPortOpen([int]$Port) {
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $c.Connect("127.0.0.1", $Port)
        $c.Close()
        return $true
    } catch {
        return $false
    }
}

function Wait-Port([int]$Port, [int]$TimeoutSec = 45) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-TcpPortOpen $Port) { return $true }
        Start-Sleep -Milliseconds 400
    }
    return $false
}

function Get-PythonCmd() {
    foreach ($name in @("python", "python3", "py")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            if ($name -eq "py") { return "py -3" }
            return $name
        }
    }
    return $null
}

function Start-LoggedProcess([string]$Title, [string]$WorkingDir, [string]$Command) {
    $wd = $WorkingDir -replace "'", "''"
    $arg = "-NoLogo -NoProfile -ExecutionPolicy Bypass -Command `"Set-Location -LiteralPath '$wd'; $Command`""
    Write-Host "[start] $Title"
    Start-Process -FilePath "powershell.exe" -ArgumentList $arg -WorkingDirectory $WorkingDir -WindowStyle Normal
}

$webStampFiles = @(
    (Join-Path $DashRoot "src\mapbox\web\mapbox-scene.js"),
    (Join-Path $DashRoot "src\mapbox\web\route-snap.js"),
    (Join-Path $DashRoot "src\mapbox\web\vehicle-layer.js"),
    (Join-Path $DashRoot "src\mapbox\web\index.html"),
    (Join-Path $DashRoot "src\mapbox\mapbox.qrc"),
    (Join-Path $DashRoot "src\ui\CockpitPanel.cpp"),
    (Join-Path $DashRoot "src\ui\CockpitMapHud.cpp")
)
$needsBuild = $Rebuild -or -not (Test-Path $Exe)
if (-not $needsBuild -and (Test-Path $Exe)) {
    $exeTime = (Get-Item $Exe).LastWriteTimeUtc
    foreach ($f in $webStampFiles) {
        if ((Test-Path $f) -and (Get-Item $f).LastWriteTimeUtc -gt $exeTime) {
            $needsBuild = $true
            break
        }
    }
}

if ($needsBuild) {
    Write-Host "Building SDV_Dashboard ($Config)..."
    Get-Process -Name "SDV_Dashboard" -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmake) { throw "cmake not found. Install CMake or build manually." }
    $buildRoot = Join-Path $DashRoot "build"
    if (-not (Test-Path $buildRoot)) {
        & cmake -S $DashRoot -B $buildRoot -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
    }
    & cmake --build $buildRoot --config $Config --target SDV_Dashboard
    if (-not (Test-Path $Exe)) { throw "Build failed: $Exe" }
    Write-Host "[ok] $Exe"
}

if (Test-Path (Join-Path $QtBin "windeployqt.exe")) {
    $env:PATH = "$QtBin;$env:PATH"
}

if (-not $SkipDeploy) {
    $wsDll = Join-Path $BuildDir "Qt6WebSockets.dll"
    if (-not (Test-Path $wsDll)) {
        Write-Host "Deploying Qt runtime DLLs (windeployqt)..."
        & (Join-Path $QtBin "windeployqt.exe") --release --websockets --webenginewidgets --no-compiler-runtime $Exe
    }
}

if (-not $NoStatusServer) {
    if (-not (Test-Path $StatusServerDir)) {
        Write-Warning "status-server not found at $StatusServerDir"
    } elseif (-not (Test-TcpPortOpen 8080)) {
        if (-not (Test-Path (Join-Path $StatusServerDir "node_modules"))) {
            Write-Host "npm install in status-server (first run)..."
            Push-Location $StatusServerDir
            & npm install --no-fund --no-audit
            Pop-Location
        }
        Start-LoggedProcess "status-server :8080" $StatusServerDir '$env:PORT=8080; npm start'
        if (-not (Wait-Port 8080)) {
            throw "status-server did not start on port 8080"
        }
        Write-Host "[ok] status-server listening on 8080"
    } else {
        Write-Host "[skip] port 8080 already in use (status-server assumed running)"
    }
}

if ($Carla) {
    $CarlaMode = "bridge"
}

if (-not $NoMockWs) {
    $wsScript =     if ($Carla -and (Test-Path $CarlaBridgeScript)) { $CarlaBridgeScript } else { $MockWsScript }
    $wsLabel = if ($Carla) { "pygame map bridge :9090" } else { "mock ROS WebSocket :9090" }
    if (-not (Test-Path $wsScript)) {
        Write-Warning "Telemetry WS script not found: $wsScript"
    } elseif (-not (Test-TcpPortOpen 9090)) {
        $py = Get-PythonCmd
        if (-not $py) {
            Write-Warning "Python not found — install Python 3 or start WS manually on :9090"
        } else {
            $wsDir = Split-Path -Parent $wsScript
            $wsFile = Split-Path -Leaf $wsScript
            Start-LoggedProcess $wsLabel $wsDir "$py '$wsFile'"
        }
        if (-not (Wait-Port 9090)) {
            Write-Warning "WS did not open port 9090 — start carla_integration_bridge.py or mock server manually"
        } else {
            Write-Host "[ok] telemetry WS on ws://127.0.0.1:9090 ($wsLabel)"
        }
    } else {
        Write-Host "[skip] port 9090 already in use (ROS bridge / CARLA / mock assumed running)"
    }
}

function Read-MapboxToken {
    if ($env:MAPBOX_TOKEN) { return $env:MAPBOX_TOKEN.Trim() }
    if ($env:MAPBOX_ACCESS_TOKEN) { return $env:MAPBOX_ACCESS_TOKEN.Trim() }
    $tokenFile = Join-Path $DashRoot ".mapbox_token"
    if (Test-Path $tokenFile) {
        $line = (Get-Content $tokenFile -Raw).Trim()
        if ($line -and -not $line.StartsWith("#")) { return $line.Trim('"').Trim("'") }
    }
    return ""
}

$mapboxToken = Read-MapboxToken
if (-not $mapboxToken) {
    Write-Host ""
    Write-Host "=== MAPBOX TOKEN REQUIRED FOR MAP TILES ===" -ForegroundColor Yellow
    Write-Host "1. Open https://account.mapbox.com/access-tokens/"
    Write-Host "2. Copy Default public token (starts with pk.)"
    Write-Host "3. Save one line to: $DashRoot\.mapbox_token"
    Write-Host "   OR: `$env:MAPBOX_TOKEN = 'pk.eyJ...'"
    Write-Host "   (Do NOT use sk. secret tokens in the app)"
    Write-Host ""
} elseif ($mapboxToken.StartsWith("sk.")) {
    Write-Error "Use a public pk. token, not sk. secret token. See .mapbox_token.example"
} elseif (-not $mapboxToken.StartsWith("pk.")) {
    Write-Warning "Token should start with pk. — check .mapbox_token"
}

if (-not $mapboxToken) {
    Write-Host ""
    Write-Host "ERROR: Mapbox token required. Map and arrow-key drive will not work." -ForegroundColor Red
    Write-Host "  Create: $DashRoot\.mapbox_token"
    Write-Host "  Content: one line starting with pk. (from https://account.mapbox.com/access-tokens/)"
    Write-Host ""
    exit 1
}

$dashArgs = @(
    "--car-id", $CarId,
    "--api-base", $ApiBase,
    "--mqtt-host", $MqttHost,
    "--mqtt-port", "$MqttPort",
    "--ros-ws-url", $RosWsUrl
)
if ($mapboxToken) {
    $dashArgs += @("--mapbox-token", $mapboxToken)
}
if ($CarlaMode -and $CarlaMode -ne "off") {
    $dashArgs += @("--carla-mode", $CarlaMode)
} elseif ($Carla) {
    $dashArgs += @("--carla-mode", "bridge")
}

Write-Host ""
Write-Host "Stopping any running SDV_Dashboard (required for map/cockpit updates)..."
Get-Process -Name "SDV_Dashboard" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
$env:SDV_FORCE_NEW = "1"

Write-Host "Launching SDV_Dashboard..."
Write-Host "  exe: $Exe"
Write-Host "  car: $CarId | api: $ApiBase | ros: $RosWsUrl"
Write-Host ""

Push-Location $BuildDir
try {
    & $Exe @dashArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
