@ECHO OFF
SET PROJECT_DIR=/mnt/c/Users/swarc/Documents/Programming/cursor/sdv-feature-flag-platform
SET BUILD_DIR=%PROJECT_DIR%/build

ECHO Cleaning up previous processes...
wsl -d Ubuntu -u root -- bash -c "pkill -f SdvPlatform; pkill -f EventBusHost; pkill -f WatchDog; pkill -f FeatureEvaluator; pkill -f DataCollector; pkill -f SignalReceiver; pkill -f SignalSender" 2>NUL
TIMEOUT /T 2 /NOBREAK > NUL

START "SDV Feature Flag Platform" /NORMAL wsl -d Ubuntu -u root -- bash -c "cd %PROJECT_DIR% && %BUILD_DIR%/SdvPlatform config/SdvPlatform.conf"
