#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <autoware_perception_msgs/msg/predicted_objects.hpp>
#include <autoware_planning_msgs/msg/trajectory.hpp>
#include <autoware_vehicle_msgs/msg/velocity_report.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>

#include "sdv/ops/domain/TelemetryEnvelope.hpp"
#include "sdv/ros_bridge/RosMessageNormalizer.hpp"
#include "sdv/ros_bridge/TelemetryWebSocketServer.hpp"

namespace sdv::ros_bridge {

class BridgeNode : public rclcpp::Node {
public:
  explicit BridgeNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~BridgeNode() override;

private:
  void publishEnvelope(const sdv::ops::domain::TelemetryEnvelope& envelope);

  bool throttleAllows(
    std::chrono::steady_clock::time_point& last_emit,
    int throttle_ms) const;

  void onVelocityReport(autoware_vehicle_msgs::msg::VelocityReport::SharedPtr msg);
  void onOdometry(nav_msgs::msg::Odometry::SharedPtr msg);
  void onTrajectory(autoware_planning_msgs::msg::Trajectory::SharedPtr msg);
  void onPredictedObjects(autoware_perception_msgs::msg::PredictedObjects::SharedPtr msg);

  RosMessageNormalizer normalizer_;
  std::unique_ptr<TelemetryWebSocketServer> websocket_;

  rclcpp::Subscription<autoware_vehicle_msgs::msg::VelocityReport>::SharedPtr sub_velocity_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr sub_odometry_;
  rclcpp::Subscription<autoware_planning_msgs::msg::Trajectory>::SharedPtr sub_trajectory_;
  rclcpp::Subscription<autoware_perception_msgs::msg::PredictedObjects>::SharedPtr sub_perception_;

  std::chrono::steady_clock::time_point last_trajectory_emit_{};
  std::chrono::steady_clock::time_point last_perception_emit_{};
  int trajectory_throttle_ms_{100};
  int perception_throttle_ms_{100};
};

}  // namespace sdv::ros_bridge
