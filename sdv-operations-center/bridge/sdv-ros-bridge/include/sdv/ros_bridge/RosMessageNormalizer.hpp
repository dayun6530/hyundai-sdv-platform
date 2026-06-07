#pragma once

#include <cstddef>

#include <autoware_perception_msgs/msg/predicted_objects.hpp>
#include <autoware_planning_msgs/msg/trajectory.hpp>
#include <autoware_vehicle_msgs/msg/velocity_report.hpp>
#include <nav_msgs/msg/odometry.hpp>

#include "sdv/ops/domain/EgoPose.hpp"
#include "sdv/ops/domain/PerceptionObjectList.hpp"
#include "sdv/ops/domain/Trajectory.hpp"
#include "sdv/ops/domain/VehicleState.hpp"

namespace sdv::ros_bridge {

class RosMessageNormalizer {
public:
  void setMaxTrajectoryPoints(std::size_t max_points);

  sdv::ops::domain::VehicleState fromVelocityReport(
    const autoware_vehicle_msgs::msg::VelocityReport& msg) const;

  sdv::ops::domain::EgoPose fromOdometry(const nav_msgs::msg::Odometry& msg) const;

  sdv::ops::domain::Trajectory fromTrajectory(
    const autoware_planning_msgs::msg::Trajectory& msg) const;

  sdv::ops::domain::PerceptionObjectList fromPredictedObjects(
    const autoware_perception_msgs::msg::PredictedObjects& msg) const;

private:
  std::size_t max_trajectory_points_{200};
};

}  // namespace sdv::ros_bridge
