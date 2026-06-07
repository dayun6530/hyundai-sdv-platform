#Requires -Version 5.1
<#
.SYNOPSIS
  Spawn SDV fleet car_1..car_10 in CARLA (hero + autopilot fleet).

.EXAMPLE
  .\spawn_sdv_fleet.ps1
  .\spawn_sdv_fleet.ps1 -FleetCount 10
#>
[CmdletBinding()]
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor",
    [ValidateRange(1, 10)]
    [int]$FleetCount = 10,
    [int]$WaitAfterSec = 8
)

$ErrorActionPreference = "Stop"

function Get-PythonCmd {
    foreach ($name in @("python", "python3", "py")) {
        if (Get-Command $name -ErrorAction SilentlyContinue) {
            if ($name -eq "py") { return "py -3" }
            return $name
        }
    }
    return $null
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$fleetPy = Join-Path $ScriptDir "spawn_sdv_fleet.py"
if (-not (Test-Path -LiteralPath $fleetPy)) {
    throw "Not found: $fleetPy"
}

$py = Get-PythonCmd
if (-not $py) {
    throw "Python not found."
}

$agentsDir = Join-Path $CarlaRoot "PythonAPI\carla\agents"
$carlaApiDir = Join-Path $CarlaRoot "PythonAPI\carla"
$pyPath = ""
if (Test-Path -LiteralPath $agentsDir) {
    $pyPath = "$agentsDir;$carlaApiDir"
}

$envPrefix = ""
if ($pyPath) {
    $envPrefix = "set `"PYTHONPATH=$pyPath`" && "
}

$cmdLine = "title SDV fleet car_1..car_$FleetCount && cd /d `"$ScriptDir`" && ${envPrefix}${py} spawn_sdv_fleet.py --count $FleetCount"
Write-Host "[spawn_sdv_fleet] Opening window: SDV fleet ($FleetCount vehicles)"
Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmdLine) -WindowStyle Normal

if ($WaitAfterSec -gt 0) {
    Write-Host "[spawn_sdv_fleet] Waiting ${WaitAfterSec}s for fleet spawn..."
    Start-Sleep -Seconds $WaitAfterSec
}

Write-Host "[spawn_sdv_fleet] Fleet script running (car_1=hero, others autopilot)." -ForegroundColor Green
