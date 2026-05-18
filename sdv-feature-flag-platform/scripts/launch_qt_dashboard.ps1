#Requires -Version 5.1
<#
.SYNOPSIS
  Launch Windows Qt SDV_Dashboard.exe (HTTP :8080, MQTT :1884, cockpit :5175).

.EXAMPLE
  .\scripts\launch_qt_dashboard.ps1 -CarId car_1
#>
[CmdletBinding()]
param(
    [string]$CarId = "car_1"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
. (Join-Path $ProjectRoot "scripts\SdvRunCommon.ps1")

Start-SdvQtDashboard -ProjectRoot $ProjectRoot -CarId $CarId -LogPrefix "launch_qt"
