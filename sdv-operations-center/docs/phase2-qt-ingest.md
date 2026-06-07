# Phase 2 — Qt ingest pipeline

Implemented in `sdv-feature-flag-platform/sdv-dashboard-qt/src/ops/`.

## Directory structure

```text
sdv-dashboard-qt/src/
├── ops/
│   ├── domain/
│   │   ├── OpsTypes.h
│   │   ├── TelemetryParser.h
│   │   └── TelemetryParser.cpp
│   ├── ingest/
│   │   ├── RosBridgeClient.h/.cpp      # QWebSocket on worker QThread
│   │   └── SignalManager.h/.cpp        # JSON → domain → Qt signals
│   ├── viewmodels/
│   │   ├── EgoViewModel.h/.cpp
│   │   ├── TrajectoryViewModel.h/.cpp
│   │   └── PerceptionViewModel.h/.cpp
│   └── OpsTelemetryPipeline.h/.cpp     # Thread wiring + ViewModels
└── examples/
    └── OpsIngestIntegrationExample.h/.cpp
```

## Thread model

```text
[QThread: SdvRosBridgeWsThread]
  RosBridgeClient (QWebSocket)
       │ jsonFrameReceived (QueuedConnection)
       ▼
[GUI thread]
  SignalManager::ingestJsonFrame
       │ vehicleStateUpdated / egoPoseUpdated / …
       ▼
  EgoViewModel / TrajectoryViewModel / PerceptionViewModel
```

## Run

```bash
# Terminal 1 — Phase 1 bridge
ros2 launch sdv_ros_bridge sdv_ros_bridge.launch.py

# Terminal 2 — Qt dashboard
./SDV_Dashboard --ros-ws-url ws://127.0.0.1:9090
```

Disable ROS ingest: `--ros-ws-url off`

## Integration example

`OpsIngestIntegrationPanel` is mounted in `MainWindow` under **ROS live telemetry**.  
`HealthMonitorWidget` and `EventTimelineWidget` subscribe to `OpsTelemetryPipeline::statusLineChanged`.

Minimal custom integration:

```cpp
#include "ops/OpsTelemetryPipeline.h"

auto* pipeline = new sdv::dash::ops::OpsTelemetryPipeline(parent);
pipeline->start(QUrl(QStringLiteral("ws://127.0.0.1:9090")));

connect(pipeline->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, [pipeline]() {
    const auto& pose = pipeline->egoViewModel()->pose();
    // feed Mapbox WebChannel: pose.x, pose.y, pose.yawRad
});
```
