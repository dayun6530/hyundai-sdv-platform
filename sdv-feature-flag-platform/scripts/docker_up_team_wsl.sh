#!/usr/bin/env bash
set -euo pipefail

TEAM_DIR="${1:?TEAM_DIR required}"
cd "${TEAM_DIR}"

# The compose file uses fixed container_name values (sdv-mosquitto, sdv-loki, ...).
# Remove stale instances first so repeated run_sdv.ps1 executions do not fail with
# "container name is already in use" after an interrupted or cross-project run.
docker rm -f \
  sdv-mosquitto \
  sdv-loki \
  sdv-grafana \
  sdv-otel-collector \
  sdv-unleash \
  sdv-unleash-postgres \
  sdv-operational-postgres \
  sdv-operational-data-api \
  sdv-pgadmin \
  >/dev/null 2>&1 || true

# All services in teamproject/docker-compose.yml (incl. pgadmin on :5050).
docker compose -p sdv up -d --remove-orphans
