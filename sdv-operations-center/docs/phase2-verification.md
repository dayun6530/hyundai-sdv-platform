# Phase 2 verification checklist

## Verified on this machine (2026-06-01)

| Layer | Status | Evidence |
|-------|--------|----------|
| JSON → SignalManager → EgoViewModel | **PASS** | `ops_ingest_smoke.exe` — x increases 126→165, `PASS (14 updates)` |
| WebSocket → RosBridgeClient → QLabel | **BLOCKED** | Qt **6.8.3** install has no `Qt6WebSockets` module |
| Autoware → sdv_ros_bridge | **NOT RUN** | ROS 2 Humble not installed in WSL |

## Fix before Mapbox (required)

1. **Qt Maintenance Tool** → Qt 6.8.3 → MSVC 2022 64-bit → add **Qt WebSockets**
2. Reconfigure & rebuild:

```powershell
cd sdv-feature-flag-platform\sdv-dashboard-qt
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
```

3. Terminal A — bridge (Linux with Autoware or mock):

```bash
# Real stack
ros2 launch sdv_ros_bridge sdv_ros_bridge.launch.py

# Or mock (Linux)
python3 sdv-operations-center/scripts/mock_telemetry_ws_server.py
```

4. Terminal B — dashboard:

```powershell
.\build\Release\SDV_Dashboard.exe --ros-ws-url ws://127.0.0.1:9090
```

5. UI must show **live** text under **ROS live telemetry** and **Health → ROS telemetry**, e.g.:

`ROS WS: connected | Ego (map) x=123.40 y=567.80 ... | Trajectory ... | Perception ...`

## Quick middle-layer test (no WebSocket)

```powershell
.\build\Release\ops_ingest_smoke.exe
```

Expect changing `Ego (map) x=...` lines and `PASS`.
