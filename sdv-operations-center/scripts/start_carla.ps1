#Requires -Version 5.1
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor",
    [int]$WaitSec = 120
)

$ErrorActionPreference = "Stop"
$exe = Join-Path $CarlaRoot "CarlaUE4.exe"
if (-not (Test-Path $exe)) { throw "Not found: $exe" }

$running = Get-Process -Name "CarlaUE4" -ErrorAction SilentlyContinue
if ($running.Count -gt 1) {
    Write-Host "WARNING: Multiple CarlaUE4 already running. Run .\stop_carla.ps1 first." -ForegroundColor Yellow
}

if (-not $running) {
    Write-Host "Starting CARLA (single instance)..."
    Start-Process -FilePath $exe `
        -ArgumentList "-dx11","-nosound","-windowed","-ResX=800","-ResY=600","-quality-level=Low" `
        -WorkingDirectory $CarlaRoot
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$waitPy = Join-Path $scriptDir "wait_for_carla.py"
if (Get-Command py -ErrorAction SilentlyContinue) {
    $pyExe = "py"
    $pyArgs = @("-3", $waitPy, "--timeout", "$WaitSec")
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $pyExe = "python"
    $pyArgs = @($waitPy, "--timeout", "$WaitSec")
} else {
    throw "Python not found"
}

Write-Host "Waiting for RPC 127.0.0.1:2000 (map load may take 30-90s)..."
& $pyExe @pyArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Verify Python API (CARLA egg from install path)
$examples = Join-Path $CarlaRoot "PythonAPI\examples"
$verify = @"
import glob, os, sys
sys.path.append(glob.glob(os.path.join('..','carla','dist','carla-*%d.%d-%s.egg' % (sys.version_info.major, sys.version_info.minor, 'win-amd64')))[0])
import carla
c = carla.Client('127.0.0.1', 2000)
c.set_timeout(20.0)
print('OK', c.get_server_version(), c.get_world().get_map().name)
"@
Push-Location $examples
try {
    if ($pyExe -eq "py") {
        & py -3 -c $verify
    } else {
        & python -c $verify
    }
    if ($LASTEXITCODE -ne 0) { exit 1 }
} finally {
    Pop-Location
}
Write-Host "CARLA ready — run generate_traffic.py now." -ForegroundColor Green
