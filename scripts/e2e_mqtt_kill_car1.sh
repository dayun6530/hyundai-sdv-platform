#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM_DIR="$ROOT_DIR/sdv-feature-flag-platform"

cd "$PLATFORM_DIR"

rm -f /tmp/telemetry.json /tmp/e2e.log /tmp/payload.json || true

cat > /tmp/payload.json <<'EOF'
{"protocol_version":"1.0","header":{"msg_id":"00000000-0000-4000-8000-000000000003","timestamp":"2026-04-25T15:00:20Z","vehicle_id":"car_1"},"command":"KILL_SWITCH","payload":{"feature_name":"AdasFeature","version":"","action":"","binary_path":""}}
EOF

timeout 35s ./build/car_1 >/tmp/e2e.log 2>&1 &
CARPID=$!

sleep 9

timeout 12s mosquitto_sub -h 127.0.0.1 -p 1884 -t /sdv/vehicle/car_1/telemetry -C 1 > /tmp/telemetry.json 2>&1 &
SUBPID=$!

sleep 0.4
mosquitto_pub -h 127.0.0.1 -p 1884 -t /sdv/vehicle/car_1/control -q 1 -f /tmp/payload.json

sleep 10

echo "---telemetry---"
cat /tmp/telemetry.json 2>/dev/null || true

kill "$CARPID" 2>/dev/null || true
kill "$SUBPID" 2>/dev/null || true

sleep 0.2
echo "---adas_tail---"
tail -n 30 logs/car_1/AdasFeature_A.log 2>/dev/null || true
echo "---feature_evaluator_tail---"
tail -n 30 logs/car_1/FeatureEvaluator.log 2>/dev/null || true

