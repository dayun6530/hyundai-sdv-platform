#Requires -Version 5.1
<#
.SYNOPSIS
  Infra-only SDV stack (same servers as run_sdv.ps1, no car_1..car_10).

.EXAMPLE
  .\run_sdv2.ps1
  .\scripts\start_car.ps1 -Car 1

  Full fleet demo: .\run_sdv.ps1
  (Windows does not run .ps2 files — use this script or run_sdv2.bat.)
#>
[CmdletBinding()]
param(
    [switch]$LaunchQt,
    [string]$QtCarId = "car_1",
    [switch]$KillCars
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$TeamRoot = (Resolve-Path (Join-Path $ProjectRoot "..")).Path
. (Join-Path $ProjectRoot "scripts\SdvRunCommon.ps1")

$WslDistro = Resolve-SdvWslDistro
if ($WslDistro) {
    Write-Host "Using WSL distro: $WslDistro (override with `$env:SDV_WSL_DISTRO)"
    $env:SDV_WSL_DISTRO = $WslDistro
} else {
    Write-Host "Using WSL default distro (override with `$env:SDV_WSL_DISTRO)"
}

$paths = Get-SdvBuildPaths -ProjectRoot $ProjectRoot
$projWsl = $paths.ProjWsl
$buildRel = $paths.BuildRel
$teamWsl = ConvertTo-SdvWslPath $TeamRoot

$wslArgsBase = Get-SdvWslArgs -WslDistro $WslDistro
$wslArgsRoot = Get-SdvWslArgs -WslDistro $WslDistro -Root

if ($KillCars) {
    Write-Host "Cleaning up previous SDV / car processes in WSL (KillCars)..."
    Invoke-SdvKillAllCarsWsl -WslArgsRoot $wslArgsRoot
    Start-Sleep -Seconds 3
    & wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/wait_shutdown_wsl.sh`" 2>/dev/null || true"
    & wsl.exe @wslArgsRoot bash "$projWsl/scripts/wait_shutdown_wsl.sh" "60" "0.3"
} else {
    Write-Host "[run_sdv2] Skipping car cleanup (existing car_* left running). Use -KillCars to stop all."
}

Write-Host "Ensuring no manual mosquitto is running in WSL (Docker broker mode)..."
$null = & wsl.exe @wslArgsRoot bash -c "pkill -x mosquitto || true" 2>$null
Start-Sleep -Milliseconds 600

& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/docker_up_team_wsl.sh`" `"$projWsl/scripts/wait_unleash_http.sh`" `"$projWsl/scripts/start_platform_wsl.sh`" 2>/dev/null || true"

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
    if ($env:WSLENV) { $env:WSLENV = "$($env:WSLENV):$piece" } else { $env:WSLENV = $piece }
}

if ($env:SDV_CMAKE_BUILD -eq "1") {
    Write-Host "Incremental cmake ($buildRel): SDV_CMAKE_BUILD=1..."
    & wsl.exe @wslArgsBase bash -lc "cd `"$projWsl`" && cmake --build `"$buildRel`" -j 4"
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "cmake --build failed (exit $LASTEXITCODE)"
    }
}

Stop-SdvListenPorts -Ports @(8080, 5170, 5173, 5174, 5175) -LogPrefix "run_sdv2"
Start-Sleep -Milliseconds 800

$viteDiag = Join-Path $ProjectRoot "scripts\diagnose_vite_ports.ps1"
if (Test-Path -LiteralPath $viteDiag) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $viteDiag -Quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Vite ports 5170, 5173-5175 may be blocked. Run: .\scripts\diagnose_vite_ports.ps1 -FixHint"
    }
}

Write-Host "Starting Node UIs + status-server (no vehicles)..."
Start-SdvOptionalCmdWindow -ProjectRoot $ProjectRoot -SubDir "status-server" -NpmScript "start"
Start-SdvOptionalCmdWindow -ProjectRoot $ProjectRoot -SubDir "admin-portal"
Start-SdvOptionalCmdWindow -ProjectRoot $ProjectRoot -SubDir "dashboard"
Start-SdvOptionalCmdWindow -ProjectRoot $ProjectRoot -SubDir "test-ui"
Start-SdvOptionalCmdWindow -ProjectRoot $ProjectRoot -SubDir "sdv-cockpit-ui"

Write-Host "Waiting for status-server to drain pending Docker OTA queue (if any)..."
Invoke-SdvStatusServerApplyPending -LogPrefix "run_sdv2"

Write-Host "Done (infra only). admin-portal: http://127.0.0.1:5170  (unified menu for dashboard, test-ui, pgAdmin, Grafana)"
Write-Host "      dashboard: http://127.0.0.1:5173  test-ui: http://127.0.0.1:5174  cockpit-ui: http://127.0.0.1:5175  pgAdmin: http://127.0.0.1:5050  Grafana: http://127.0.0.1:3000"
Write-Host "[run_sdv2] No car_1..car_10 started. Next: .\scripts\start_car.ps1 -Car 1"

if ($LaunchQt -or $env:SDV_LAUNCH_QT -eq "1") {
    Start-SdvQtDashboard -ProjectRoot $ProjectRoot -CarId $QtCarId -LogPrefix "run_sdv2"
} else {
    Write-Host "[run_sdv2] Qt skipped (start a car first, then: .\scripts\launch_qt_dashboard.ps1 -CarId car_1)"
}
