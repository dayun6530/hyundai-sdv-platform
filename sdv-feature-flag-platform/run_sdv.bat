@ECHO OFF
REM Unicode paths (e.g. Korean folder names) break cmd.exe FOR /F + temp wslpath files.
REM All orchestration runs from run_sdv.ps1 (UTF-16 / Unicode-safe argv to wsl.exe).
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_sdv.ps1"
EXIT /B %ERRORLEVEL%
