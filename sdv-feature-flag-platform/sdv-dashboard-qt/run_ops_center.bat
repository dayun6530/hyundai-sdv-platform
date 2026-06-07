@echo off
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run_ops_center.ps1" %*
exit /b %ERRORLEVEL%
