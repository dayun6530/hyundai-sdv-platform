# SDV Digital Twin Architecture

Hyundai SDV Operations Center — CARLA today, Autoware tomorrow.  
**Telemetry pipeline is frozen:** `RosBridgeClient` → `SignalManager::ingestJsonFrame` → ViewModels.

All twin-specific data flows through **adapter interfaces** and optional parallel paths (`DigitalTwinController`, `ingestJsonFrame` with existing envelope types only).

---

## Layered architecture

```mermaid
flowchart TB
  subgraph sources [Data sources]
    CARLA[CARLA Python API]
    AW[Autoware ROS2 future]
  end

  subgraph adapters [Twin adapters Phase E]
    LA[LocalizationAdapter]
    PA[PerceptionAdapter]
    PLA[PlanningAdapter]
    CA[ControlAdapter]
  end

  subgraph carla_path [CARLA path today]
    PY[carla_integration_bridge.py]
    WS9090[WS :9090 Phase-1]
    WS9092[WS :9092 snapshot]
    WS9093[WS :9093 control]
    BR[CarlaDataBridge]
  end

  subgraph frozen [Unchanged telemetry core]
    SM[SignalManager]
    VM[Ego / Trajectory / Perception ViewModels]
  end

  subgraph presentation [Presentation]
    DTC[DigitalTwinController]
    COCK[CarlaCockpitView + FSD overlays]
    MAP[MapboxBridge + MiniMap]
  end

  CARLA --> PY
  PY --> WS9090
  PY --> WS9092
  PY --> WS9093
  WS9092 --> BR
  BR --> SM
  WS9090 --> SM
  BR --> DTC
  DTC --> LA
  DTC --> PA
  DTC --> PLA
  DTC --> CA
  SM --> VM
  VM --> COCK
  DTC --> COCK
  DTC --> MAP
  MAP -->|destination pick| WS9093
  AW -.-> adapters
```

---

## Class diagram (C++ twin layer)

```mermaid
classDiagram
  class ITwinLocalizationAdapter {
    <<interface>>
    +egoPose() EgoPose
    +speedLimitKmh() double
  }
  class ITwinPerceptionAdapter {
    <<interface>>
    +objects() PerceptionObjectList
    +trafficLights() vector~TwinTrafficLight~
  }
  class ITwinPlanningAdapter {
    <<interface>>
    +plannedRoute() Trajectory
    +navManeuver() QString
    +distanceToGoalM() double
    +etaSec() double
    +routeProgress() double
  }
  class ITwinControlAdapter {
    <<interface>>
    +setDestination(x,y)
    +startAutonomous(mode)
    +pauseAutonomous()
    +resumeAutonomous()
    +emergencyStop()
  }

  class CarlaTwinAdapters {
    +localization() ITwinLocalizationAdapter*
    +perception() ITwinPerceptionAdapter*
    +planning() ITwinPlanningAdapter*
    +control() ITwinControlAdapter*
  }

  class DigitalTwinController {
    -worldState TwinWorldState
    +bindPipeline(OpsTelemetryPipeline*)
    +bindMapbox(MapboxBridge*)
    +onSnapshot(CarlaWorldSnapshot)
    +onDestinationPicked(x,y)
  }

  class CarlaDataBridge {
    +snapshotReady(CarlaWorldSnapshot)
    +telemetryFrameReady(QByteArray)
  }

  class SignalManager {
    +ingestJsonFrame(QByteArray)
  }

  ITwinLocalizationAdapter <|.. CarlaLocalizationAdapter
  ITwinPerceptionAdapter <|.. CarlaPerceptionAdapter
  ITwinPlanningAdapter <|.. CarlaPlanningAdapter
  ITwinControlAdapter <|.. CarlaControlAdapter
  CarlaTwinAdapters *-- CarlaLocalizationAdapter
  CarlaTwinAdapters *-- CarlaPerceptionAdapter
  CarlaTwinAdapters *-- CarlaPlanningAdapter
  CarlaTwinAdapters *-- CarlaControlAdapter
  DigitalTwinController --> CarlaTwinAdapters
  DigitalTwinController --> CarlaDataBridge : listens
  CarlaDataBridge --> SignalManager : ingestJsonFrame
```

---

## Phase mapping

| Phase | Deliverable | Pipeline impact |
|-------|-------------|-----------------|
| **A** Full CARLA | Python ego/traffic/peds/lights/speed; `traffic_light` in `perception_objects` | None — same envelopes |
| **B** Route planning | Mapbox click → control WS → GRP route → `trajectory` + map GeoJSON | None |
| **C** Autonomous | BasicAgent / BehaviorAgent; Start/Pause/Resume via control WS | None |
| **D** Visualization | FSD overlays + `TrafficLightOverlayRenderer` + twin HUD fields | None |
| **E** Autoware | `ITwin*Adapter` stubs; swap `CarlaTwinAdapters` → `AutowareTwinAdapters` | None |

---

## Ports

| Port | Protocol |
|------|----------|
| 2000 | CARLA RPC |
| 9090 | Phase-1 telemetry + camera |
| 9092 | `carla_snapshot` (rich twin metadata) |
| 9093 | `carla_control` + `carla_status` |

---

## Integration guide

### 1. Start CARLA + bridge

```bash
# Terminal 1 — CARLA 0.9.15
CarlaUE4.exe

# Terminal 2
cd sdv-operations-center/scripts
python carla_integration_bridge.py
# Expect: Connected to CARLA — map Town10HD
#          CARLA Control Center v2 + Digital Twin sensors
```

### 2. Dashboard

```bash
sdv-dashboard-qt/build/Release/SDV_Dashboard.exe --carla-mode bridge
```

- **Shift+click** on minimap → destination (local X/Y → CARLA route).
- **AUTONOMOUS** panel: Start / Pause / Resume / E-STOP.
- Cockpit focus → WASD manual drive when autonomy off.

### 3. Modes

| `--carla-mode` | Twin source |
|----------------|-------------|
| `bridge` | Python → 9090 ingest + 9093 control |
| `carla-ws` | 9092 snapshot → C++ bridge → ingest |
| `sim` | C++ sim + local control (no CARLA) |

### 4. Switching to Autoware (future)

1. Implement `AutowareTwinAdapters` (subscribe ROS2 → same `ITwin*` outputs).
2. In `MainWindow`, replace `CarlaTwinAdapters` factory with Autoware factory.
3. **Do not** change `SignalManager`, ViewModels, or overlay widgets.

---

## Snapshot JSON extensions (`carla_snapshot.data`)

```json
{
  "vehicle": { ... },
  "actors": [ ... ],
  "traffic_lights": [ { "id", "state", "position", "yaw_rad" } ],
  "speed_limit_kmh": 50,
  "autonomy": { "mode": "behavior", "state": "driving", "maneuver": "lane_follow" },
  "navigation": { "remain_m", "eta_sec", "progress", "arrow" },
  "trajectory_points": [ ... ]
}
```

Traffic lights are duplicated into `perception_objects` as `classification: "traffic_light"` for overlay compatibility.
