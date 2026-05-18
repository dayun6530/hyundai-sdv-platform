#Requires -Version 5.1
<#
.SYNOPSIS
  Start a single SDV vehicle platform (car_N) in a foreground WSL CMD window.

.EXAMPLE
  .\scripts\start_car.ps1 -Car 1

  Opens "car_1" console running build_unleash/car_1 + config/car/car_1.conf
  (same as one iteration inside run_sdv.ps1). Run .\run_sdv2.ps1 first for Docker / status-server.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateRange(1, 10)]
    [int]$Car,
    [switch]$SkipQt
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $ProjectRoot "scripts\SdvRunCommon.ps1")

$carName = "car_$Car"
$carConf = "config/car/${carName}.conf"
$confPath = Join-Path $ProjectRoot ($carConf -replace '/', '\')
if (-not (Test-Path -LiteralPath $confPath)) {
    throw "Missing config: $confPath"
}

$WslDistro = Resolve-SdvWslDistro
if ($WslDistro) { $env:SDV_WSL_DISTRO = $WslDistro }

$paths = Get-SdvBuildPaths -ProjectRoot $ProjectRoot
$projWsl = $paths.ProjWsl
$buildWsl = $paths.BuildWsl
$buildRel = $paths.BuildRel

$wslArgsRoot = Get-SdvWslArgs -WslDistro $WslDistro -Root
& wsl.exe @wslArgsRoot bash -lc "sed -i 's/\r$//' `"$projWsl/scripts/start_platform_wsl.sh`" 2>/dev/null || true"

$carBinWsl = "$buildWsl/$carName"
$check = & wsl.exe @wslArgsRoot bash -lc "test -x '$carBinWsl' && echo ok" 2>$null
if ($check -notmatch 'ok') {
    throw "Executable not found in WSL: $carBinWsl — build with: cmake --build $buildRel"
}

Write-Host "[start_car] Opening $carName ($buildRel/$carName + $carConf)"
$bashLine = "cd '$projWsl' && bash '$projWsl/scripts/start_platform_wsl.sh' '$projWsl' '$buildWsl' '$carConf' --foreground"
$wslCmd = "title $carName && wsl.exe -u root -- bash -lc `"$bashLine`""
Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $wslCmd) -WindowStyle Normal

if (-not $SkipQt) {
  Start-Sleep -Seconds 2
  Start-SdvQtDashboard -ProjectRoot $ProjectRoot -CarId $carName -LogPrefix "start_car"
} else {
  Write-Host "[start_car] Qt skipped (-SkipQt). Manual: .\scripts\launch_qt_dashboard.ps1 -CarId $carName"
}
