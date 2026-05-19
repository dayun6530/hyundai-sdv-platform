#Requires -Version 5.1
<#
.SYNOPSIS
  Build Windows Qt SDV_Dashboard.exe (requires Qt 6 + MSVC + CMake).

.EXAMPLE
  .\scripts\build_qt_dashboard.ps1
  .\scripts\build_qt_dashboard.ps1 -Config Release
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [string]$QtPrefixPath = "",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$QtDir = Join-Path $ProjectRoot "sdv-dashboard-qt"
$OutDir = Join-Path $QtDir $BuildDir

function Find-Qt6Prefix {
    param([string]$Hint)
    if ($Hint -and (Test-Path -LiteralPath (Join-Path $Hint "lib\cmake\Qt6\Qt6Config.cmake"))) {
        return (Resolve-Path -LiteralPath $Hint).Path
    }
    if ($env:CMAKE_PREFIX_PATH) {
        foreach ($part in $env:CMAKE_PREFIX_PATH -split ";") {
            if ($part -and (Test-Path -LiteralPath (Join-Path $part "lib\cmake\Qt6\Qt6Config.cmake"))) {
                return (Resolve-Path -LiteralPath $part).Path
            }
        }
    }
    $roots = @("C:\Qt", "$env:USERPROFILE\Qt")
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $found = Get-ChildItem -Path $root -Recurse -Filter "Qt6Config.cmake" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\msvc\d+_64\\lib\\cmake\\Qt6\\Qt6Config.cmake$" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($found) {
            return (Resolve-Path -LiteralPath (Join-Path $found.Directory.Parent.Parent.Parent.FullName)).Path
        }
    }
    return $null
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found in PATH. Install CMake or use 'Developer PowerShell for VS'."
}

$qtPrefix = Find-Qt6Prefix -Hint $QtPrefixPath
if (-not $qtPrefix) {
    throw @"
Qt 6 (MSVC 64-bit) not found.
Install Qt 6 with: Qt Widgets, Qt WebEngine, optional Qt MQTT.
Then run:
  .\scripts\build_qt_dashboard.ps1 -QtPrefixPath 'C:\Qt\6.x.x\msvc2022_64'
"@
}

Write-Host "Qt6: $qtPrefix"
Write-Host "Build dir: $OutDir ($Config)"

if (-not (Test-Path -LiteralPath $OutDir)) {
    cmake -S $QtDir -B $OutDir -DCMAKE_PREFIX_PATH="$qtPrefix" -DCMAKE_BUILD_TYPE=$Config
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "Reusing existing build folder (delete $OutDir to reconfigure from scratch)."
}

cmake --build $OutDir --config $Config -j 4
if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }

$exe = Join-Path $OutDir "$Config\SDV_Dashboard.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    $exe = Join-Path $OutDir "SDV_Dashboard.exe"
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build finished but SDV_Dashboard.exe not found under $OutDir"
}

$windeploy = Join-Path $qtPrefix "bin\windeployqt.exe"
if (Test-Path -LiteralPath $windeploy) {
    Write-Host "Running windeployqt (Qt runtime DLLs next to exe)..."
    & $windeploy --release "$exe" 2>&1 | Out-Host
}

Write-Host ""
Write-Host "OK: $exe"
Write-Host "Launch: .\open_qt_dashboard.bat  OR  .\scripts\launch_qt_dashboard.ps1 -CarId car_1"
Write-Host "Skip Qt on run_sdv: `$env:SDV_SKIP_QT_DASHBOARD='1'"
