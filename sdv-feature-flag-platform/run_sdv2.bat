@ECHO OFF
REM Infra-only: Docker + status-server + Vite (no car_1..car_10). Then: scripts\start_car.ps1 -Car 1
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_sdv2.ps1" %*
EXIT /B %ERRORLEVEL%
