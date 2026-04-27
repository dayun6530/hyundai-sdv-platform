#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${1:?PROJECT_DIR required}"
BUILD_DIR="${2:?BUILD_DIR required}"
CONFIG_REL="${3:?CONFIG_REL required}"

cd "${PROJECT_DIR}"

PLATFORM=""
if [[ "${CONFIG_REL}" =~ ^config/car/(car_[0-9]+)\.conf$ ]]; then
  car_bin="${BASH_REMATCH[1]}"
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
echo "[start_platform_wsl] platform=${PLATFORM} config=${CONFIG_REL}" >&2

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

exec "${PLATFORM}" "${CONFIG_REL}"
