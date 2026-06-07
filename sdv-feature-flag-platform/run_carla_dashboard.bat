@ECHO OFF
REM One-click CARLA + pygame bridge + Qt Dashboard (Unicode-safe via PowerShell).
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_carla_dashboard.ps1" %*
EXIT /B %ERRORLEVEL%
