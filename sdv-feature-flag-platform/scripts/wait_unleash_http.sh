#!/usr/bin/env bash
set -euo pipefail

for ((i = 1; i <= 30; i++)); do
  if curl -fsS http://127.0.0.1:4242/ >/dev/null 2>&1; then
    exit 0
  fi
  sleep 1
done
exit 0
