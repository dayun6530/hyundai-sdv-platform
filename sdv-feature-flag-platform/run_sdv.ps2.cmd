@ECHO OFF
REM Alias for run_sdv2.ps1 (Windows cannot execute .ps2 PowerShell scripts directly).
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_sdv2.ps1" %*
EXIT /B %ERRORLEVEL%
