# SDV Operations Center

Production-oriented operations stack for Hyundai SDV Feature Operations Center.

## Phase 1 — `sdv-ros-bridge`

Standalone ROS 2 Humble bridge: Autoware topics → normalized domain JSON → WebSocket (`ws://0.0.0.0:9090` by default).

### Build (WSL / Ubuntu 22.04 + ROS 2 Humble)

```bash
source /opt/ros/humble/setup.bash
# Autoware message packages (from Autoware install or rosdep)
sudo apt install -y \
  ros-humble-autoware-vehicle-msgs \
  ros-humble-autoware-planning-msgs \
  ros-humble-autoware-perception-msgs \
  nlohmann-json3-dev libboost-system-dev

cd /mnt/c/Users/82107/Desktop/TP4/sdv-operations-center
colcon build --packages-select sdv_ops_data_core sdv_ros_bridge
source install/setup.bash
```

### Run

```bash
ros2 run sdv_ros_bridge sdv_ros_bridge_node --ros-args \
  --params-file $(ros2 pkg prefix sdv_ros_bridge)/share/sdv_ros_bridge/config/bridge_params.yaml
```

### WebSocket clients

Connect to `ws://127.0.0.1:9090`. Each telemetry message is one JSON text frame:

```json
{
  "schema_version": 1,
  "type": "ego_pose",
  "stamp_ns": 1710000000000000000,
  "frame_id": "map",
  "data": { ... }
}
```

Types: `vehicle_state`, `ego_pose`, `trajectory`, `perception_objects`.

### Class diagram

See [docs/phase1-class-diagram.md](docs/phase1-class-diagram.md).
