# Phase 1 — Directory structure

```text
sdv-operations-center/
├── README.md
├── docs/
│   ├── phase1-class-diagram.md
│   ├── phase1-component-diagram.md
│   └── phase1-directory-structure.md
├── adapters/
│   └── data-core/                          # ament package: sdv_ops_data_core
│       ├── CMakeLists.txt
│       ├── package.xml
│       ├── include/sdv/ops/domain/
│       │   ├── TimeConv.hpp
│       │   ├── VehicleState.hpp
│       │   ├── EgoPose.hpp
│       │   ├── Trajectory.hpp
│       │   ├── PerceptionObjectList.hpp
│       │   └── TelemetryEnvelope.hpp
│       └── src/
│           ├── TimeConv.cpp
│           ├── VehicleState.cpp
│           ├── EgoPose.cpp
│           ├── Trajectory.cpp
│           ├── PerceptionObjectList.cpp
│           └── TelemetryEnvelope.cpp
└── bridge/
    └── sdv-ros-bridge/                      # ament package: sdv_ros_bridge
        ├── CMakeLists.txt
        ├── package.xml
        ├── config/
        │   └── bridge_params.yaml
        ├── launch/
        │   └── sdv_ros_bridge.launch.py
        ├── include/sdv/ros_bridge/
        │   ├── BridgeNode.hpp
        │   ├── RosMessageNormalizer.hpp
        │   └── TelemetryWebSocketServer.hpp
        └── src/
            ├── main.cpp
            ├── BridgeNode.cpp
            ├── RosMessageNormalizer.cpp
            └── TelemetryWebSocketServer.cpp
```
