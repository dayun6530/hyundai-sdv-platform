#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${1:?PROJECT_DIR required}"
BUILD_DIR="${2:?BUILD_DIR required}"
CONFIG_REL="${3:?CONFIG_REL required}"
CONFIG_REL_ARG="${CONFIG_REL}"
MODE="${4:-}"

cd "${PROJECT_DIR}"

# Platform-style deploy artifacts are owned by sdv-ota-manager (sibling repo dir).
# Keep compatibility by exporting SDV_PLATFORM_DEPLOY_ROOT when not explicitly set.
if [[ -z "${SDV_PLATFORM_DEPLOY_ROOT:-}" ]]; then
  OTA_PLATFORM_DEPLOY="$(cd "${PROJECT_DIR}/.." && pwd)/sdv-ota-manager/platform/deploy"
  if [[ -d "${OTA_PLATFORM_DEPLOY}" ]]; then
    export SDV_PLATFORM_DEPLOY_ROOT="${OTA_PLATFORM_DEPLOY}"
  fi
fi

PLATFORM=""
LOG_TAG="SdvPlatform"
if [[ "${CONFIG_REL}" =~ ^config/car/(car_[0-9]+)\.conf$ ]]; then
  car_bin="${BASH_REMATCH[1]}"
  LOG_TAG="${car_bin}"
  if [[ -x "${BUILD_DIR}/${car_bin}" ]]; then
    PLATFORM="${BUILD_DIR}/${car_bin}"
  else
    echo "[start_platform_wsl] ERROR: '${BUILD_DIR}/${car_bin}' not found/executable (CONFIG_REL=${CONFIG_REL})" >&2
    exit 127
  fi
else
  PLATFORM="${BUILD_DIR}/SdvPlatform"
  CONFIG_REL="config/SdvPlatform.conf"
fi

# Print what we're about to run (helps confirm car_1 vs SdvPlatform).
echo "[start_platform_wsl] platform=${PLATFORM} config=${CONFIG_REL} (arg=${CONFIG_REL_ARG})" >&2

# Telemetry export defaults (DataCollector child process inherits env).
export SDV_OTLP_LOGS_ENDPOINT="${SDV_OTLP_LOGS_ENDPOINT:-http://127.0.0.1:4318/v1/logs}"
export SDV_OPERATIONAL_DB_INGEST_URL="${SDV_OPERATIONAL_DB_INGEST_URL:-http://127.0.0.1:8090/v1/ingest}"

# Ensure log directory exists (repo-relative, works regardless of caller cwd).
mkdir -p logs
# LifeCycleMgr resolves fleet shadow policy from this file (see removeInactiveShadowFeaturesUsingFleetState).
if [[ "${LOG_TAG}" =~ ^car_[0-9]+$ ]] && [[ -n "${SDV_PLATFORM_DEPLOY_ROOT:-}" ]]; then
  export SDV_FLEET_DEPLOY_STATE_PATH="${SDV_PLATFORM_DEPLOY_ROOT}/state/${LOG_TAG}.json"
fi
START_LOG="logs/start_${LOG_TAG}.log"

# If 127.0.0.1:1884 is closed inside WSL, default SDV_MQTT_BROKER_URI to Windows host gateway.
if [[ -z "${SDV_MQTT_BROKER_URI:-}" ]] && grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null; then
  if bash -c 'exec 3<>/dev/tcp/127.0.0.1/1884' 2>/dev/null; then
    :
  else
    win_host="$(ip route show 2>/dev/null | awk '/^default/ {print $3; exit}')"
    if [[ -z "${win_host}" ]]; then
      win_host="$(awk '/^nameserver / {print $2; exit}' /etc/resolv.conf 2>/dev/null || true)"
    fi
    if [[ -n "${win_host}" ]]; then
      export SDV_MQTT_BROKER_URI="tcp://${win_host}:1884"
      echo "[start_platform_wsl] SDV_MQTT_BROKER_URI=${SDV_MQTT_BROKER_URI}" >&2
    fi
  fi
fi

{
  echo "=== $(date -Is) ==="
  echo "[start_platform_wsl] platform=${PLATFORM}"
  echo "[start_platform_wsl] config=${CONFIG_REL}"
  echo "[start_platform_wsl] pwd=$(pwd)"
  echo "[start_platform_wsl] user=$(id -un) uid=$(id -u)"
  echo "[start_platform_wsl] SDV_MQTT_BROKER_URI=${SDV_MQTT_BROKER_URI:-<unset>}"
  echo "[start_platform_wsl] mode=${MODE:-<default>}"
} >> "${START_LOG}" 2>&1

# Foreground mode: keep this WSL console attached/open and stream logs.
if [[ "${MODE}" == "--foreground" ]]; then
  # Fleet / Docker version lines are appended by status-server to logs/<car>/version_events.log.
  # Tail them here so the car CMD window shows deploy activity without restarting the binary.
  if [[ "${LOG_TAG}" =~ ^car_ ]]; then
    mkdir -p "logs/${LOG_TAG}"
    EVT="logs/${LOG_TAG}/version_events.log"
    : >>"$EVT"
    if [[ -r /dev/tty ]]; then
      nohup tail -n0 -F "$EVT" >/dev/tty 2>/dev/null &
    else
      nohup tail -n0 -F "$EVT" >&2 2>/dev/null &
    fi
    disown || true
  fi
  exec "${PLATFORM}" "${CONFIG_REL}"
fi

# Detach from the wsl.exe launcher so the process survives after the Windows-side
# wsl.exe window closes (and so startup errors are visible in START_LOG).
nohup "${PLATFORM}" "${CONFIG_REL}" >> "${START_LOG}" 2>&1 < /dev/null &
PID="$!"
disown || true

{
  echo "[start_platform_wsl] started pid=${PID} (log=${START_LOG})"
  if kill -0 "${PID}" 2>/dev/null; then
    echo "[start_platform_wsl] pid ${PID} is running"
  else
    echo "[start_platform_wsl] pid ${PID} is NOT running (exited immediately)"
  fi
} >> "${START_LOG}" 2>&1

echo "[start_platform_wsl] started pid=${PID} (log=${START_LOG})" >&2
exit 0
