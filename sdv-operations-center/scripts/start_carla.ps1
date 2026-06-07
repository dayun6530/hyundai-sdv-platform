#Requires -Version 5.1
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor",
    [int]$WaitSec = 120,
    [int]$ApiVerifySec = 90
)

$ErrorActionPreference = "Stop"
$exe = Join-Path $CarlaRoot "CarlaUE4.exe"
if (-not (Test-Path $exe)) { throw "Not found: $exe" }

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$running = @(Get-Process -Name "CarlaUE4" -ErrorAction SilentlyContinue)
if ($running.Count -gt 1) {
    Write-Host "WARNING: Multiple CarlaUE4 already running. Run .\stop_carla.ps1 first." -ForegroundColor Yellow
}

if ($running.Count -eq 0) {
    Write-Host "Starting CARLA (single instance)..."
    Start-Process -FilePath $exe `
        -ArgumentList "-dx11","-nosound","-windowed","-ResX=800","-ResY=600","-quality-level=Low" `
        -WorkingDirectory $CarlaRoot
} else {
    Write-Host "CARLA already running ($($running.Count) CarlaUE4) — waiting for API (map may still be loading)..."
}

$waitPy = Join-Path $scriptDir "wait_for_carla.py"
$verifyPy = Join-Path $scriptDir "verify_carla_ready.py"
if (Get-Command py -ErrorAction SilentlyContinue) {
    $pyExe = "py"
    $waitArgs = @("-3", $waitPy, "--timeout", "$WaitSec")
    $verifyArgs = @("-3", $verifyPy, "--timeout", "$ApiVerifySec", "--carla-root", $CarlaRoot)
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $pyExe = "python"
    $waitArgs = @($waitPy, "--timeout", "$WaitSec")
    $verifyArgs = @($verifyPy, "--timeout", "$ApiVerifySec", "--carla-root", $CarlaRoot)
} else {
    throw "Python not found"
}

Write-Host "Waiting for RPC 127.0.0.1:2000 (map load may take 30-90s)..."
& $pyExe @waitArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Port open != world ready — retry Python API (fixes flaky 20s one-shot verify).
Write-Host "Verifying CARLA Python API (up to ${ApiVerifySec}s)..."
& $pyExe @verifyArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "If CARLA was left running from an earlier session, reset it:" -ForegroundColor Yellow
    Write-Host "  .\stop_carla.ps1" -ForegroundColor Yellow
    Write-Host "  .\start_carla.ps1" -ForegroundColor Yellow
    exit 1
}

Write-Host "CARLA ready — run_carla_dashboard.ps1 will spawn generate_traffic.py, or:" -ForegroundColor Green
Write-Host "  .\spawn_carla_traffic.ps1 -VehicleCount 10" -ForegroundColor Yellow
