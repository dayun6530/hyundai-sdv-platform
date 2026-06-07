#Requires -Version 5.1
<#
  Fix "time-out while waiting for the simulator" on 127.0.0.1:2000.

  Usually: CarlaUE4 not running, or MULTIPLE CarlaUE4 instances (port conflict).

  Usage:
    .\stop_carla.ps1
    .\start_carla.ps1
    # wait until map loads, then:
    python generate_traffic.py -n 30 -w 30
#>
param(
    [string]$CarlaRoot = "C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor"
)

$ErrorActionPreference = "SilentlyContinue"
$names = @("CarlaUE4", "CarlaUE4-Win64-Shipping")
$killed = 0
foreach ($n in $names) {
    Get-Process -Name $n | ForEach-Object {
        Write-Host "Stopping $($_.ProcessName) (PID $($_.Id))"
        Stop-Process -Id $_.Id -Force
        $killed++
    }
}
Start-Sleep -Seconds 2
$still = Get-Process -Name $names[0], $names[1] -ErrorAction SilentlyContinue
if ($still) {
    Write-Host "Some CARLA processes still running — close windows manually." -ForegroundColor Yellow
} else {
    Write-Host "All CARLA processes stopped ($killed)." -ForegroundColor Green
}
Write-Host "Port 2000 should be free. Start ONE instance: .\start_carla.ps1"
