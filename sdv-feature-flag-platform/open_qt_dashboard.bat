@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\launch_qt_dashboard.ps1" -CarId car_1
pause
