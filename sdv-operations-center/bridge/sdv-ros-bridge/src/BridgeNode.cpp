#include "sdv/ros_bridge/BridgeNode.hpp"

#include <stdexcept>
#include <utility>

namespace sdv::ros_bridge {

BridgeNode::BridgeNode(const rclcpp::NodeOptions& options)
: Node("sdv_ros_bridge_node", options)
{
  const auto host = declare_parameter<std::string>("websocket_host", "0.0.0.0");
  const auto port = static_cast<uint16_t>(declare_parameter<int>("websocket_port", 9090));

  const auto topic_velocity = declare_parameter<std::string>(
    "topic_vehicle_velocity", "/vehicle/status/velocity_status");
  const auto topic_localization = declare_parameter<std::string>(
    "topic_localization", "/localization/kinematic_state");
  const auto topic_trajectory = declare_parameter<std::string>(
    "topic_trajectory", "/planning/scenario_planning/trajectory");
  const auto topic_perception = declare_parameter<std::string>(
    "topic_perception_objects", "/perception/object_recognition/objects");

  const auto max_traj_points = static_cast<std::size_t>(
    declare_parameter<int>("max_trajectory_points", 200));
  trajectory_throttle_ms_ = declare_parameter<int>("trajectory_throttle_ms", 100);
  perception_throttle_ms_ = declare_parameter<int>("perception_throttle_ms", 100);

  normalizer_.setMaxTrajectoryPoints(max_traj_points);

  websocket_ = std::make_unique<TelemetryWebSocketServer>();
  std::string ws_error;
  if (!websocket_->start(host, port, &ws_error)) {
    RCLCPP_FATAL(get_logger(), "WebSocket server failed: %s", ws_error.c_str());
    throw std::runtime_error(ws_error);
  }

  RCLCPP_INFO(
    get_logger(), "Telemetry WebSocket listening on ws://%s:%u (clients=%zu)",
    host.c_str(), port, websocket_->clientCount());

  const auto sensor_qos = rclcpp::SensorDataQoS();

  sub_velocity_ = create_subscription<autoware_vehicle_msgs::msg::VelocityReport>(
    topic_velocity, sensor_qos,
    std::bind(&BridgeNode::onVelocityReport, this, std::placeholders::_1));

  sub_odometry_ = create_subscription<nav_msgs::msg::Odometry>(
    topic_localization, sensor_qos,
    std::bind(&BridgeNode::onOdometry, this, std::placeholders::_1));

  sub_trajectory_ = create_subscription<autoware_planning_msgs::msg::Trajectory>(
    topic_trajectory, sensor_qos,
    std::bind(&BridgeNode::onTrajectory, this, std::placeholders::_1));

  sub_perception_ = create_subscription<autoware_perception_msgs::msg::PredictedObjects>(
    topic_perception, sensor_qos,
    std::bind(&BridgeNode::onPredictedObjects, this, std::placeholders::_1));

  RCLCPP_INFO(get_logger(), "Subscribed to Autoware topics:");
  RCLCPP_INFO(get_logger(), "  velocity:   %s", topic_velocity.c_str());
  RCLCPP_INFO(get_logger(), "  localization: %s", topic_localization.c_str());
  RCLCPP_INFO(get_logger(), "  trajectory: %s", topic_trajectory.c_str());
  RCLCPP_INFO(get_logger(), "  perception: %s", topic_perception.c_str());
}

BridgeNode::~BridgeNode()
{
  if (websocket_) {
    websocket_->stop();
  }
}

void BridgeNode::publishEnvelope(const sdv::ops::domain::TelemetryEnvelope& envelope)
{
  if (!websocket_) {
    return;
  }
  websocket_->broadcast(envelope.toJsonString());
}

bool BridgeNode::throttleAllows(
  std::chrono::steady_clock::time_point& last_emit, const int throttle_ms) const
{
  if (throttle_ms <= 0) {
    return true;
  }
  const auto now = std::chrono::steady_clock::now();
  if (last_emit.time_since_epoch().count() == 0) {
    last_emit = now;
    return true;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_emit);
  if (elapsed.count() < throttle_ms) {
    return false;
  }
  last_emit = now;
  return true;
}

void BridgeNode::onVelocityReport(autoware_vehicle_msgs::msg::VelocityReport::SharedPtr msg)
{
  const auto model = normalizer_.fromVelocityReport(*msg);
  publishEnvelope(sdv::ops::domain::makeEnvelope(
    sdv::ops::domain::TelemetryType::VehicleState,
    model.stamp_ns, model.frame_id, model.toJson()));
}

void BridgeNode::onOdometry(nav_msgs::msg::Odometry::SharedPtr msg)
{
  const auto model = normalizer_.fromOdometry(*msg);
  publishEnvelope(sdv::ops::domain::makeEnvelope(
    sdv::ops::domain::TelemetryType::EgoPose,
    model.stamp_ns, model.frame_id, model.toJson()));
}

void BridgeNode::onTrajectory(autoware_planning_msgs::msg::Trajectory::SharedPtr msg)
{
  if (!throttleAllows(last_trajectory_emit_, trajectory_throttle_ms_)) {
    return;
  }
  const auto model = normalizer_.fromTrajectory(*msg);
  publishEnvelope(sdv::ops::domain::makeEnvelope(
    sdv::ops::domain::TelemetryType::Trajectory,
    model.stamp_ns, model.frame_id, model.toJson()));
}

void BridgeNode::onPredictedObjects(autoware_perception_msgs::msg::PredictedObjects::SharedPtr msg)
{
  if (!throttleAllows(last_perception_emit_, perception_throttle_ms_)) {
    return;
  }
  const auto model = normalizer_.fromPredictedObjects(*msg);
  publishEnvelope(sdv::ops::domain::makeEnvelope(
    sdv::ops::domain::TelemetryType::PerceptionObjects,
    model.stamp_ns, model.frame_id, model.toJson()));
}

}  // namespace sdv::ros_bridge
