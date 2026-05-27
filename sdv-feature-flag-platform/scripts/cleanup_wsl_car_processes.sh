#!/usr/bin/env bash
# Kill leftover SDV / car_N processes before run_sdv (invoked from PowerShell via WSL).
set +e
pkill -9 -f SdvPlatform 2>/dev/null
for i in $(seq 10 -1 1); do
  pkill -9 -f "/build_unleash/car_${i}([[:space:]]|$)" 2>/dev/null
  pkill -9 -f "/build/car_${i}([[:space:]]|$)" 2>/dev/null
done
pkill -9 -f 'sdv-feature-flag-platform/build_unleash/car_' 2>/dev/null
pkill -9 -f 'sdv-feature-flag-platform/build/car_' 2>/dev/null
pkill -9 -f EventBusHost 2>/dev/null
pkill -9 -f WatchDog 2>/dev/null
pkill -9 -f FeatureEvaluator 2>/dev/null
pkill -9 -f DataCollector 2>/dev/null
pkill -9 -f SignalReceiver 2>/dev/null
pkill -9 -f SignalSender 2>/dev/null
pkill -9 -f MqttVehicleAgentHost 2>/dev/null
pkill -9 -f NavigationFeature 2>/dev/null
pkill -9 -f AdasFeature 2>/dev/null
pkill -9 -f EvEnergyFeature 2>/dev/null
pkill -9 -f DeploymentMgr 2>/dev/null
exit 0
