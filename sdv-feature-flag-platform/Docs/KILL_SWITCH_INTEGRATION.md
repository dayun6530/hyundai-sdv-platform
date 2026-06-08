# Integrated Kill Switch — Test Guide

Unified flow: **Dashboard Kill**, **MQTT TEST Kill**, and **status-server API** all update
`deploy/state`, `fleet_history.jsonl`, and (optionally) runtime MQTT `KILL:{Feature}`.

## Prerequisites

1. MQTT broker (`mqtt://127.0.0.1:1884`, WebSocket `ws://127.0.0.1:9001`)
2. `status-server` on `:8080`
3. Dashboard (`5173`) and/or Admin Portal MQTT TEST (`5174`)
4. Vehicle runtime with SignalReceiver (e.g. `run_sdv.ps1`)

## Feature mapping

| UI label   | Runtime MQTT name   | deploy/state key |
|------------|---------------------|------------------|
| ADAS       | `AdasFeature`       | `ADAS`           |
| Navigation | `NavigationFeature` | `Navigation`     |
| EV Energy  | `EvEnergyFeature`   | `Energy`         |

## Test: MQTT TEST Kill ON

1. Admin Portal → MQTT TEST → select `car_1`
2. Click **ADAS KILL**
3. Verify:
   - Log: `Runtime KILL sent: KILL:AdasFeature`
   - Log: `Deploy state synced: ADAS KILL ON`
   - Runtime: KillSwitch event / feature stop
   - `sdv-ota-manager/platform/deploy/state/car_1.json` → `features.ADAS.kill_switch: true`
   - `deploy/history/fleet_history.jsonl` → `action: "kill-switch"`
   - Dashboard → car_1 ADAS **KILL ON**
4. Repeat for Navigation and Energy independently — other features unchanged

## Test: Dashboard Kill ON / OFF

1. Fleet Dashboard → Vehicle Detail → Operational Actions
2. **Apply Kill Switch** → state + history + MQTT runtime dispatch
3. **Clear Kill Switch** → `kill_switch: false`, `cleared_at`, history `kill-switch-clear`
4. Clear does **not** restart Feature runtime

## API

```http
POST /api/fleet/kill-switch
Content-Type: application/json

{
  "car_id": "car_1",
  "feature": "ADAS",
  "runtime_feature": "AdasFeature",
  "kill": true,
  "reason": "manual kill switch",
  "source": "dashboard",
  "send_runtime": true,
  "apply_fleet_kill_policy": true,
  "sync_config": false
}
```

Kill OFF: `"kill": false`, `"send_runtime": false`

## Paths to inspect

- `sdv-ota-manager/platform/deploy/state/car_*.json`
- `sdv-ota-manager/platform/deploy/history/fleet_history.jsonl`
- Vehicle logs / MQTT inbound topic `/sdv/vehicle/{car_id}/signals/inbound`
