#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${1:?PROJECT_DIR required}"
BUILD_DIR="${2:?BUILD_DIR required}"
LOG_DIR="${3:?LOG_DIR required}"
FROM="${4:-1}"
TO="${5:-10}"

mkdir -p "${LOG_DIR}"
cd "${PROJECT_DIR}"

missing=0
for i in $(seq "${FROM}" "${TO}"); do
  bin="${BUILD_DIR}/car_${i}"
  if [[ ! -x "${bin}" ]]; then
    echo "[run_cars_wsl] MISSING/NOT-EXECUTABLE: ${bin}" >&2
    missing=1
  fi
done
if [[ "${missing}" -ne 0 ]]; then
  exit 127
fi

for i in $(seq "${FROM}" "${TO}"); do
  car="car_${i}"
  conf="config/car/${car}.conf"
  log="${LOG_DIR}/${car}.log"
  echo "[run_cars_wsl] starting ${car} (conf=${conf} log=${log})" >&2

  nohup bash "${PROJECT_DIR}/scripts/start_platform_wsl.sh" "${PROJECT_DIR}" "${BUILD_DIR}" "${conf}" > "${log}" 2>&1 &
  pid="$!"
  disown || true
  echo "[run_cars_wsl] started ${car} pid=${pid}" >&2

  # Immediate-crash check: give it a moment, then verify the main process exists.
  sleep 0.2
  if ! pgrep -af "${BUILD_DIR}/${car}" >/dev/null 2>&1; then
    echo "[run_cars_wsl] ${car} is not running. Last log lines:" >&2
    tail -n 80 "${log}" >&2 || true
  fi

  # Stagger startups to reduce port/process churn during teardown/startup.
  sleep 0.6
done

