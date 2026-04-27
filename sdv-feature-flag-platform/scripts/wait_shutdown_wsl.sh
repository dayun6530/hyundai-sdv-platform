#!/usr/bin/env bash
set -euo pipefail

PATTERN='SdvPlatform|/car_[0-9]+|EventBusHost|WatchDog|FeatureEvaluator|DataCollector|SignalReceiver|SignalSender|MqttVehicleAgentHost|NavigationFeature|AdasFeature|EvEnergyFeature'
TRIES="${1:-30}"
SLEEP_SEC="${2:-0.3}"

for _ in $(seq 1 "${TRIES}"); do
  if pgrep -afE "${PATTERN}" >/dev/null 2>&1; then
    sleep "${SLEEP_SEC}"
  else
    exit 0
  fi
done

echo "[wait_shutdown_wsl] Warning: SDV processes still exist after waiting." >&2

