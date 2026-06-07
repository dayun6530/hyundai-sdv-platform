# Validation closure report

Date: 2026-05-27  
Scope: Close FAIL / NOT IMPLEMENTED items from the digital-twin validation pass (no architecture redesign).

Legend: **PASS** = verified in this session with command output or code-path proof. **FAIL** = blocked on environment (CARLA/agents/ports). **E2E-PENDING** = build/static OK; needs live stack.

---

## 1. CARLA agents installation verification

| Component | Verdict | Evidence |
|-----------|---------|----------|
| `carla` Python module | **PASS** | `verify_carla_agents.py` exit 1, line `carla PASS` |
| `GlobalRoutePlanner` | **FAIL** | `ModuleNotFoundError: No module named 'agents'` (CARLA_ROOT not set) |
| `BasicAgent` | **FAIL** | same |
| `BehaviorAgent` | **FAIL** | same |

**Source files**

- `sdv-operations-center/scripts/verify_carla_agents.py`
- `sdv-operations-center/scripts/carla_integration_bridge.py` (GRP fallback in `set_destination`)

**Test procedure**

```powershell
python sdv-operations-center/scripts/verify_carla_agents.py
# With CARLA installed:
$env:CARLA_ROOT = "C:\path\to\CARLA_0.9.15"
python sdv-operations-center/scripts/verify_carla_agents.py
```

**Expected PASS (all four)** when `CARLA_ROOT\PythonAPI\carla\agents` is on the path: exit code `0`.

**Captured output (this host, 2026-05-27)**

```
carla                  PASS
GlobalRoutePlanner     FAIL (ModuleNotFoundError: No module named 'agents')
BasicAgent             FAIL (ModuleNotFoundError: No module named 'agents')
BehaviorAgent          FAIL (ModuleNotFoundError: No module named 'agents')
```

---

## 2. Autonomous driving (START AUTO, destination UX, agent state)

| Item | Verdict | Evidence |
|------|---------|----------|
| START AUTO without destination (no ego) | **PASS** (static) | `CarlaControlManager::startAutonomousDriving` returns `false`, status message |
| Default goal 80 m ahead when ego valid | **PASS** (static) | Sets destination from `EgoViewModel` |
| Bridge default dest in Python | **PASS** (static) | `CarlaAutonomyManager._default_destination_ahead` + `start()` |
| UI: agent mode (basic/behavior) | **PASS** (build) | `CarlaControlPanel` combo + `setAutonomousAgent` |
| UI: agent status label | **PASS** (build) | `agentStatusChanged` → `AGENT mode · state · maneuver` |
| UI: destination hint | **PASS** (build) | `destinationHintChanged` |
| UI: blocked START feedback | **PASS** (build) | Panel sets `AGENT blocked — set DEST` on `false` |
| E2E BehaviorAgent drive | **E2E-PENDING** | Requires agents PASS + bridge :9093 + CARLA |

**Source files**

- `sdv-dashboard-qt/src/carla/control/CarlaControlManager.cpp`, `.h`
- `sdv-dashboard-qt/src/carla/control/CarlaControlPanel.cpp`, `.h`
- `sdv-dashboard-qt/src/carla/control/CarlaVehicleController.cpp`, `.h`
- `sdv-dashboard-qt/src/carla/control/CarlaControlBridge.cpp`
- `sdv-operations-center/scripts/carla_integration_bridge.py`
- `sdv-dashboard-qt/src/MainWindow.cpp` (passes `opsPipeline_` into `configure`)

**Test procedure (E2E)**

1. Set `CARLA_ROOT`, run `verify_carla_agents.py` → exit 0.
2. Start CARLA + `carla_integration_bridge.py` (9090/9092/9093).
3. `SDV_Dashboard.exe --carla-mode bridge`
4. Shift+click minimap → destination, or ROUTE spinboxes + ROUTE.
5. Select **behavior** or **basic**, click **START AUTO**.
6. Confirm HUD/control chip shows `AGENT behavior · driving · …` and vehicle moves.

**Build evidence**

```
SDV_Dashboard.vcxproj -> ...\build\Release\SDV_Dashboard.exe  (cmake --build Release, exit 0)
```

---

## 3. Traffic light state integration (RED / YELLOW / GREEN)

| Path | Verdict | Evidence |
|------|---------|----------|
| `:9090` perception `tl_state` ingest | **PASS** (static) | `TelemetryParser.cpp` → `PerceptionObject.trafficLightState` |
| Bridge emits `tl_state` | **PASS** (static) | `carla_integration_bridge.py` perception objects |
| Cockpit overlay colors | **PASS** (build) | `TrafficLightOverlayRenderer` uses `trafficLightState` |
| Minimap / main map circles | **PASS** (build) | `minimap.js`, `mapbox-scene.js` match on `tl_state` |
| Twin snapshot lights | **PASS** (static) | `DigitalTwinController` → `setTrafficLights` (unchanged) |
| E2E colored dots on live stream | **E2E-PENDING** | Needs bridge + CARLA traffic lights |

**Source files**

- `sdv-dashboard-qt/src/ops/domain/OpsTypes.h`, `TelemetryParser.cpp`
- `sdv-operations-center/scripts/carla_integration_bridge.py`
- `sdv-dashboard-qt/src/carla/TrafficLightOverlayRenderer.cpp`
- `sdv-dashboard-qt/src/carla/PerceptionOverlayRenderer.cpp` (skips TL class in 3D boxes)
- `sdv-dashboard-qt/src/mapbox/web/minimap.js`, `mapbox-scene.js`

**Test procedure (E2E)**

1. Run bridge; confirm WS frame contains `"tl_state":"red"` (or yellow/green) on traffic_light objects.
2. Open dashboard bridge mode; verify minimap circles and cockpit TL overlay match state.

---

## 4. Full Mapbox scene

| Item | Verdict | Evidence |
|------|---------|----------|
| `MapboxCockpitView` wired in UI | **PASS** (build) | `CockpitPanel` stacks view; Operations/Dashboard toggles visibility |
| `egoPoseChanged` → map | **PASS** (static) | `mapbox-scene.js` calls `followEgo` |
| `perceptionChanged` → map | **PASS** (static) | `applyPerceptionGeoJson` |
| `trajectoryChanged` / `plannedRouteChanged` | **PASS** (static) | layers + handlers |
| Live ego push (ops/dashboard camera) | **PASS** (static) | `MapboxCockpitView::pushViewModelsToBridge` |
| E2E map with live CARLA | **E2E-PENDING** | Mapbox token + bridge required |

**Source files**

- `sdv-dashboard-qt/src/ui/CockpitPanel.cpp`, `.h`
- `sdv-dashboard-qt/src/MainWindow.cpp`
- `sdv-dashboard-qt/src/mapbox/MapboxCockpitView.cpp` (added to `CMakeLists.txt`)
- `sdv-dashboard-qt/src/mapbox/web/mapbox-scene.js`
- `sdv-dashboard-qt/src/mapbox/MapboxGeoJson.h`

**Test procedure**

1. Build dashboard; ensure `.mapbox_token` has `pk.` token.
2. Launch dashboard; top bar → **Operations** or **Dashboard** (full map visible).
3. With bridge running, ego dot tracks pose; perception layer shows actors; planned route from twin snapshot (:9092).

---

## 5. Route visualization consistency

| Surface | Verdict | Evidence |
|---------|---------|----------|
| Trajectory → minimap (lng/lat) | **PASS** (build) | `CarlaCockpitView::pushMiniMapTelemetry` + `MapboxGeoJson` |
| Trajectory → main map | **PASS** (build) | `mapbox-scene.js` `applyTrajectoryGeoJson` |
| Planned route → minimap | **PASS** (static) | `plannedRouteChanged` in `minimap.js` |
| Planned route → main map | **PASS** (static) | `mapbox-scene.js` |
| Twin route push | **PASS** (static) | `DigitalTwinController::pushRouteToMap` |
| Cockpit overlay route | **PASS** (static) | `NavigationOverlayRenderer` (unchanged) |

**Source files**

- `sdv-dashboard-qt/src/carla/CarlaCockpitView.cpp`
- `sdv-dashboard-qt/src/mapbox/web/minimap.js`
- `sdv-dashboard-qt/src/mapbox/web/mapbox-scene.js`
- `sdv-dashboard-qt/src/twin/DigitalTwinController.cpp`

**Test procedure**

1. Bridge mode with route points in twin snapshot.
2. Compare minimap green trajectory, cyan planned route, and Operations map layers — same WGS84 projection (`mapOriginLng/Lat`).

---

## Core telemetry smoke (regression)

| Test | Verdict | Evidence |
|------|---------|----------|
| `ops_ingest_smoke.exe` | **PASS** | Exit 0, `PASS: SignalManager -> EgoViewModel (14 updates)` |

**Command**

```powershell
.\sdv-dashboard-qt\build\Release\ops_ingest_smoke.exe
```

---

## Summary

| Priority area | Closed in code/build | Needs environment for E2E PASS |
|---------------|----------------------|--------------------------------|
| Agents verify | Script + bridge GRP path | `CARLA_ROOT` + agents wheel |
| Autonomous UX | Yes | Live CARLA + agents |
| TL states | Yes | Live perception stream |
| Full Mapbox | Yes | Token + bridge + Operations view |
| Route consistency | Yes | Live :9090/:9092 |

Do **not** mark agents or BehaviorAgent E2E as complete until `verify_carla_agents.py` exits 0 and a recorded drive test succeeds.
