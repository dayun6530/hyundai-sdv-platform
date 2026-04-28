#!/usr/bin/env bash
# Foreground launcher for a single car_N (used from Windows: one console per car).
set -euo pipefail
PROJECT_DIR="${1:?PROJECT_DIR (WSL path) required}"
BUILD_DIR="${2:?BUILD_DIR (WSL path) required}"
CAR_INDEX="${3:?car index 1..10 required}"
exec bash "${PROJECT_DIR}/scripts/start_platform_wsl.sh" "${PROJECT_DIR}" "${BUILD_DIR}" "config/car/car_${CAR_INDEX}.conf" --foreground
