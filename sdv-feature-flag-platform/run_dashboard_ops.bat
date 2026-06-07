@echo off
REM One-click: status-server + mock WS + Qt dashboard (from repo root)
cd /d "%~dp0sdv-dashboard-qt"
call run_ops_center.bat %*
exit /b %ERRORLEVEL%
