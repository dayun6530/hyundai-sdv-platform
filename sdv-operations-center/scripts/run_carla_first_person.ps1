#Requires -Version 5.1
<#
  CARLA + SDV Dashboard with pygame map in cockpit main view.

  1) CarlaUE4.exe
  2) generate_traffic.py (optional)
  3) pygame_map_bridge.py  → ws://127.0.0.1:9090 front_camera (pygame hero map)
  4) SDV_Dashboard.exe --carla-mode bridge

  Do NOT run CARLA's examples/no_rendering_mode.py alongside this.
#>
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor",
    [string]$CarlaHost = "127.0.0.1",
    [int]$CarlaPort = 2000,
    [switch]$StartCarla,
    [switch]$SpawnTraffic,
    [switch]$NoRendering,
    [switch]$LaunchDashboard,
    [string]$DashExe = "",
    [string]$MapboxToken = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$MapBridge = Join-Path $ScriptDir "pygame_map_bridge.py"
$CarlaExamples = Join-Path $CarlaRoot "PythonAPI\examples"
$CarlaExe = Join-Path $CarlaRoot "CarlaUE4.exe"

function Get-PythonCmd() {
    foreach ($name in @("python", "python3", "py")) {
        if (Get-Command $name -ErrorAction SilentlyContinue) {
            if ($name -eq "py") { return "py -3" }
            return $name
        }
    }
    return $null
}

function Test-Port([int]$Port) {
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $c.Connect("127.0.0.1", $Port)
        $c.Close()
        return $true
    } catch { return $false }
}

function Start-Window([string]$Title, [string]$Wd, [string]$Cmd) {
    $wdEsc = $Wd -replace "'", "''"
    $arg = "-NoLogo -NoProfile -ExecutionPolicy Bypass -Command `"Set-Location -LiteralPath '$wdEsc'; $Cmd`""
    Write-Host "[start] $Title"
    Start-Process powershell.exe -ArgumentList $arg -WorkingDirectory $Wd -WindowStyle Normal
}

$py = Get-PythonCmd
if (-not $py) { throw "Python not found" }
if (-not (Test-Path $MapBridge)) { throw "Missing $MapBridge" }

$env:CARLA_ROOT = $CarlaRoot
$agentsDir = Join-Path $CarlaRoot "PythonAPI\carla\agents"
$carlaApiDir = Join-Path $CarlaRoot "PythonAPI\carla"
if (Test-Path $agentsDir) { $env:PYTHONPATH = "$agentsDir;$carlaApiDir" }

if ($StartCarla) {
    if (-not (Test-Path $CarlaExe)) { throw "CARLA not found: $CarlaExe" }
    Start-Window "CARLA UE4" $CarlaRoot ".\CarlaUE4.exe -dx11 -nosound -windowed -ResX=800 -ResY=600 -quality-level=Low"
    Write-Host "Waiting for CARLA RPC :$CarlaPort ..."
    $deadline = (Get-Date).AddSeconds(90)
    while ((Get-Date) -lt $deadline) {
        if (Test-Port $CarlaPort) { break }
        Start-Sleep -Seconds 2
    }
    if (-not (Test-Port $CarlaPort)) { throw "CARLA did not open port $CarlaPort" }
}

if ($SpawnTraffic) {
    if (-not (Test-Path $CarlaExamples)) { throw "Examples dir missing: $CarlaExamples" }
    Start-Window "generate_traffic" $CarlaExamples "$py generate_traffic.py -n 30 -w 30"
    Start-Sleep -Seconds 8
}

$bridgeArgs = "$py '$MapBridge' --host $CarlaHost --port $CarlaPort"
if ($NoRendering) { $bridgeArgs += " --no-rendering" }

if (-not (Test-Port 9090)) {
    Start-Window "pygame map -> Dashboard :9090" $ScriptDir $bridgeArgs
    Start-Sleep -Seconds 4
} else {
    Write-Host "[skip] :9090 already in use"
}

Write-Host ""
Write-Host "=== pygame map in Dashboard cockpit ===" -ForegroundColor Cyan
Write-Host "  Bridge: pygame_map_bridge.py (hidden pygame, JPEG on :9090)"
Write-Host "  Dashboard: SDV_Dashboard.exe --carla-mode bridge  (1st Person main view)"
Write-Host ""

if ($LaunchDashboard) {
    if (-not $DashExe) {
        $DashExe = Join-Path (Split-Path -Parent (Split-Path -Parent $ScriptDir)) `
            "sdv-feature-flag-platform\sdv-dashboard-qt\build\Release\SDV_Dashboard.exe"
    }
    if (-not (Test-Path $DashExe)) { throw "Dashboard exe not found: $DashExe" }
    $args = @("--carla-mode", "bridge", "--ros-ws-url", "ws://127.0.0.1:9090")
    if ($MapboxToken) { $args += @("--mapbox-token", $MapboxToken) }
    & $DashExe @args
}
