# Phase 3 — Mapbox cockpit

## Layout

```text
sdv-dashboard-qt/
├── src/mapbox/
│   ├── MapboxSceneBridge.*     # QWebChannel object `sdvMap`
│   ├── SceneJsonBuilder.*      # ViewModels → JSON
│   ├── MapboxCockpitView.*     # Main 3D map (pitch 60°)
│   └── MiniMapView.*           # Synced top-down mini map
└── resources/web/
    ├── index.html
    ├── minimap.html
    ├── bridge.js
    ├── mapbox-scene.js
    └── minimap.js
```

## Run

```powershell
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
$env:MAPBOX_ACCESS_TOKEN = "pk.your_token_here"

cd sdv-feature-flag-platform\sdv-dashboard-qt
cmake --build build --config Release
.\build\Release\SDV_Dashboard.exe --ros-ws-url ws://127.0.0.1:9090
```

Without `MAPBOX_ACCESS_TOKEN`, the UI shows a banner; ego/trajectory layers still update when the map loads.

## Data flow

`EgoViewModel` / `TrajectoryViewModel` / `PerceptionViewModel` → `SceneJsonBuilder` → `MapboxSceneBridge::frameUpdated` → Mapbox GL JS (main + mini).

Coordinates use the Autoware **map frame** (local x/y as Mapbox lng/lat for demo visualization).
