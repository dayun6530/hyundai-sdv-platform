#Requires -Version 5.1
<#
.SYNOPSIS
  Spawn NPC vehicles in a running CARLA sim (generate_traffic.py).

.EXAMPLE
  .\spawn_carla_traffic.ps1
  .\spawn_carla_traffic.ps1 -VehicleCount 10
  .\spawn_carla_traffic.ps1 -VehicleCount 30 -WalkerCount 30
#>
[CmdletBinding()]
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor",
    [ValidateRange(0, 200)]
    [int]$VehicleCount = 10,
    [ValidateRange(0, 200)]
    [int]$WalkerCount = -1,
    [int]$WaitAfterSec = 6
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

$CarlaExamples = Join-Path $CarlaRoot "PythonAPI\examples"
$trafficScript = Join-Path $CarlaExamples "generate_traffic.py"
if (-not (Test-Path -LiteralPath $trafficScript)) {
    throw "Not found: $trafficScript"
}

$py = Get-PythonCmd
if (-not $py) {
    throw "Python not found. Install Python 3 or add it to PATH."
}

$agentsDir = Join-Path $CarlaRoot "PythonAPI\carla\agents"
$carlaApiDir = Join-Path $CarlaRoot "PythonAPI\carla"
$pyPath = ""
if (Test-Path -LiteralPath $agentsDir) {
    $pyPath = "$agentsDir;$carlaApiDir"
}

$trafficArgs = "generate_traffic.py -n $VehicleCount"
if ($WalkerCount -ge 0) {
    $trafficArgs += " -w $WalkerCount"
}

$envPrefix = ""
if ($pyPath) {
    $envPrefix = "set `"PYTHONPATH=$pyPath`" && "
}

$cmdLine = "title CARLA generate_traffic (-n $VehicleCount) && cd /d `"$CarlaExamples`" && ${envPrefix}${py} $trafficArgs"
Write-Host "[spawn_carla_traffic] Opening window: CARLA NPC traffic (-n $VehicleCount)"
Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmdLine) -WindowStyle Normal

if ($WaitAfterSec -gt 0) {
    Write-Host "[spawn_carla_traffic] Waiting ${WaitAfterSec}s for NPC spawn..."
    Start-Sleep -Seconds $WaitAfterSec
}

Write-Host "[spawn_carla_traffic] Traffic script started (keep its window open while driving)." -ForegroundColor Green
