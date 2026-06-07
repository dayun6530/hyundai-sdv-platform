# Copy Qt runtime DLLs next to SDV_Dashboard.exe (fixes missing Qt6WebSockets.dll etc.)
param(
    [string]$Config = "Release",
    [string]$QtBin = "C:\Qt\6.8.3\msvc2022_64\bin"
)

$ErrorActionPreference = "Stop"
$dashRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $dashRoot "build\$Config\SDV_Dashboard.exe"

if (-not (Test-Path $exe)) {
    Write-Error "Build first: cmake --build build --config $Config"
}

$windeployqt = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    Write-Error "windeployqt not found at $windeployqt — set -QtBin to your Qt msvc bin folder"
}

$env:PATH = "$QtBin;$env:PATH"
& $windeployqt --release --websockets --webenginewidgets --no-compiler-runtime $exe
Write-Host "Deployed Qt libs to: $(Split-Path $exe)"
Write-Host "Run: $exe"
