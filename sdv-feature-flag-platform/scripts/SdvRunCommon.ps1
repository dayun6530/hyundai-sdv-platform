# Shared helpers for run_sdv.ps1 / run_sdv.ps2 / start_car.ps1
#Requires -Version 5.1

function Resolve-SdvWslDistro {
    if ($env:SDV_WSL_DISTRO -and $env:SDV_WSL_DISTRO.Trim().Length -gt 0) {
        return $env:SDV_WSL_DISTRO.Trim()
    }
    try {
        $distros = & wsl.exe -l -q 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $distros) { throw "wsl list failed" }
        $distros = $distros | ForEach-Object { $_.Trim() } | Where-Object { $_ }
        if ($distros -contains "Ubuntu") { return "Ubuntu" }
        $ubuntuLike = $distros | Where-Object { $_ -like "Ubuntu*" } | Select-Object -First 1
        if ($ubuntuLike) { return $ubuntuLike }
        return ($distros | Select-Object -First 1)
    } catch {
        return ""
    }
}

function ConvertTo-SdvWslPath {
    param([Parameter(Mandatory)][string]$WindowsPath)
    $resolved = (Resolve-Path -LiteralPath $WindowsPath).Path
    if ($resolved -notmatch '^[A-Za-z]:\\') {
        throw "Unsupported path for WSL conversion: $resolved"
    }
    $drive = $resolved.Substring(0, 1).ToLowerInvariant()
    $rest = $resolved.Substring(3) -replace '\\', '/'
    return "/mnt/$drive/$rest"
}

function Get-SdvBuildPaths {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $marker = Join-Path $ProjectRoot "build_unleash\SdvPlatform"
    $projWsl = ConvertTo-SdvWslPath $ProjectRoot
    if (Test-Path -LiteralPath $marker) {
        return @{
            ProjWsl  = $projWsl
            BuildWsl = "$projWsl/build_unleash"
            BuildRel = "build_unleash"
        }
    }
    return @{
        ProjWsl  = $projWsl
        BuildWsl = "$projWsl/build"
        BuildRel = "build"
    }
}

function Get-SdvWslArgs {
    param([string]$WslDistro, [switch]$Root)
    $args = @()
    if ($WslDistro) { $args += @("-d", $WslDistro) }
    if ($Root) { $args += @("-u", "root", "--") }
    return $args
}

function Stop-SdvListenPorts {
    param([int[]]$Ports, [string]$LogPrefix = "run_sdv")
    foreach ($port in $Ports) {
        try {
            $owners = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty OwningProcess -Unique
            foreach ($procId in $owners) {
                if ($procId -and $procId -gt 0) {
                    Write-Host "[$LogPrefix] Port $port in use by PID $procId - stopping so listeners can bind."
                    Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
                }
            }
        } catch { }
    }
}

function Start-SdvOptionalCmdWindow {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][string]$SubDir,
        [string]$NpmScript = "dev"
    )
    $dir = Join-Path $ProjectRoot $SubDir
    $pkg = Join-Path $dir "package.json"
    if (-not (Test-Path -LiteralPath $pkg)) { return }
    $cmd = "cd /d `"$dir`" && npm install && npm run $NpmScript"
    Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", $cmd) -WindowStyle Normal
}

function Invoke-SdvKillAllCarsWsl {
    param($WslArgsRoot)
    $cleanupBash = @'
set +e
pkill -9 -f SdvPlatform 2>/dev/null
for i in $(seq 10 -1 1); do
  pkill -9 -f "/build_unleash/car_${i}([[:space:]]|$)" 2>/dev/null
  pkill -9 -f "/build/car_${i}([[:space:]]|$)" 2>/dev/null
done
pkill -9 -f 'sdv-feature-flag-platform/build_unleash/car_' 2>/dev/null
pkill -9 -f 'sdv-feature-flag-platform/build/car_' 2>/dev/null
pkill -9 -f EventBusHost 2>/dev/null
pkill -9 -f WatchDog 2>/dev/null
pkill -9 -f FeatureEvaluator 2>/dev/null
pkill -9 -f DataCollector 2>/dev/null
pkill -9 -f SignalReceiver 2>/dev/null
pkill -9 -f SignalSender 2>/dev/null
pkill -9 -f MqttVehicleAgentHost 2>/dev/null
pkill -9 -f NavigationFeature 2>/dev/null
pkill -9 -f AdasFeature 2>/dev/null
pkill -9 -f EvEnergyFeature 2>/dev/null
pkill -9 -f DeploymentMgr 2>/dev/null
exit 0
'@
    $null = & wsl.exe @WslArgsRoot bash -c $cleanupBash 2>$null
}

function Get-SdvDashboardExe {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $dashRoot = Join-Path $ProjectRoot "sdv-dashboard-qt\build"
    $dashCandidates = @(
        (Join-Path $dashRoot "Release\SDV_Dashboard.exe"),
        (Join-Path $dashRoot "RelWithDebInfo\SDV_Dashboard.exe"),
        (Join-Path $dashRoot "Debug\SDV_Dashboard.exe"),
        (Join-Path $dashRoot "SDV_Dashboard.exe")
    )
    return ($dashCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)
}

function Start-SdvQtDashboard {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [string]$CarId = "car_1",
        [string]$LogPrefix = "run_sdv"
    )
    $dashExe = Get-SdvDashboardExe -ProjectRoot $ProjectRoot
    if (-not $dashExe) {
        Write-Warning "[$LogPrefix] SDV_Dashboard.exe not found. Build: cmake -S sdv-dashboard-qt -B sdv-dashboard-qt/build && cmake --build sdv-dashboard-qt/build --config Release"
        return
    }

    $dashProc = Get-Process -Name "SDV_Dashboard" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($dashProc) {
        Write-Host "[$LogPrefix] SDV_Dashboard already running (PID $($dashProc.Id)) — switching to $CarId"
    } else {
        Write-Host "[$LogPrefix] Launching Qt SDV dashboard: $dashExe (car-id=$CarId)"
    }

    # Second instance forwards --car-id to the first via QLocalSocket and exits immediately.
    Start-Process -FilePath $dashExe -ArgumentList @(
        "--api-base", "http://127.0.0.1:8080",
        "--mqtt-host", "127.0.0.1",
        "--mqtt-port", "1884",
        "--car-id", $CarId
    ) -WorkingDirectory (Split-Path -Parent $dashExe) | Out-Null
}

function Invoke-SdvStatusServerApplyPending {
    param([string]$LogPrefix = "run_sdv")
    Write-Host "[$LogPrefix] Waiting for status-server on :8080..."
    $healthOk = $false
    for ($i = 0; $i -lt 40; $i++) {
        try {
            $h = Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/vehicle-runtime/health" -UseBasicParsing -TimeoutSec 2 -ErrorAction Stop
            if ($h.StatusCode -eq 200) {
                $healthOk = $true
                break
            }
        } catch { }
        Start-Sleep -Milliseconds 500
    }
    if ($healthOk) {
        try {
            $resp = Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/vehicle-runtime/apply-pending" -Method POST -UseBasicParsing -TimeoutSec 300 -ErrorAction Stop
            Write-Host "[$LogPrefix] apply-pending: $($resp.Content)"
        } catch {
            Write-Warning "[$LogPrefix] apply-pending skipped or failed: $_"
        }
    } else {
        Write-Warning "[$LogPrefix] status-server not ready; skipped apply-pending"
    }
}
