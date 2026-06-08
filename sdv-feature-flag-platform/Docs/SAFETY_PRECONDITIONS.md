# OTA Safety Preconditions

## Overview

OTA safety checks use **simulated values from `deploy/state/car_*.json`**, not live vehicle sensors.

- **Current source:** `state-json` or `manual-demo` (Prepare Demo READY)
- **Future source:** `vehicle-signal` (MQTT / Signal I/F) → same `normalizeSafetySnapshot()` → same `evaluateSafetyPreconditions()`

## Checks (default thresholds)

| Check | Required |
|-------|----------|
| Vehicle speed | 0 km/h |
| Gear | P |
| Ignition | OFF |
| Battery | ≥ 30% |
| Driver approval | true |
| Network | CONNECTED |
| OTA update_mode | READY |
| vehicle_mode | PARKED |
| parking_brake | ON |
| target_system_running | false |

Thresholds: `status-server/config/safetyThresholds.json`

## API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/fleet/safety/:carId` | Snapshot + evaluation |
| POST | `/api/fleet/safety/:carId/update` | Patch safety values |
| POST | `/api/fleet/safety/:carId/prepare-demo-ready` | Demo READY preset |
| POST | `/api/fleet/safety/:carId/assert-ready` | Pre-flight gate |

Legacy (still supported): `/api/vehicles/:carId/safety-state`

## Test scenarios

### 1. READY state

1. Open Campaign → Safety Preconditions for `car_1`
2. Click **Prepare Demo READY**
3. Confirm all checks PASS, badge **READY**
4. Run Candidate / Promote succeeds

### 2. NOT READY

1. Set `speed` to 10 or `driver_approved` to false → Apply manual values
2. Badge **NOT READY**, failed checks listed
3. Run Candidate → blocked with `failed_checks` in API/UI

### 3. Rollback safety

1. Rollback panel → safety NOT READY
2. Apply Rollback → HTTP 409, `safety_blocked: true`
3. **Prepare Demo READY for Rollback** → Apply succeeds

### 4. Source display

- After Prepare Demo READY → `Source: Manual demo`
- Fresh read from state file → `Source: State JSON simulation`

## fleet_history examples

```json
{"action":"safety-prepare-demo-ready","car_id":"car_1","source":"manual-demo","ready":true,"result":"success"}
```

```json
{"action":"safety-blocked","car_id":"car_1","requested_action":"promote","failed_checks":["speed"],"result":"blocked"}
```

## Extension (real vehicle data)

```
vehicle signals → normalizeSafetySnapshot(raw, 'vehicle-signal')
                → evaluateSafetyPreconditions(snapshot, thresholds)
                → assertSafetyReady / campaign-run gate
```

Judgment logic stays in `safetyPreconditionsApi.js`; only the snapshot loader changes.
