#include "sdv/ros_bridge/RosMessageNormalizer.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "sdv/ops/domain/TimeConv.hpp"

namespace sdv::ros_bridge {

namespace {

std::string uuidToHex(const unique_identifier_msgs::msg::UUID& uuid)
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (const auto byte : uuid.uuid) {
    oss << std::setw(2) << static_cast<int>(byte);
  }
  return oss.str();
}

std::string classificationLabel(const autoware_perception_msgs::msg::ObjectClassification& cls)
{
  using autoware_perception_msgs::msg::ObjectClassification;
  switch (cls.label) {
    case ObjectClassification::UNKNOWN:
      return "UNKNOWN";
    case ObjectClassification::CAR:
      return "CAR";
    case ObjectClassification::TRUCK:
      return "TRUCK";
    case ObjectClassification::BUS:
      return "BUS";
    case ObjectClassification::TRAILER:
      return "TRAILER";
    case ObjectClassification::MOTORCYCLE:
      return "MOTORCYCLE";
    case ObjectClassification::BICYCLE:
      return "BICYCLE";
    case ObjectClassification::PEDESTRIAN:
      return "PEDESTRIAN";
    default:
      return "OTHER";
  }
}

std::string bestClassification(const autoware_perception_msgs::msg::PredictedObject& object)
{
  if (object.classification.empty()) {
    return "UNKNOWN";
  }

  const auto best = std::max_element(
    object.classification.begin(), object.classification.end(),
    [](const auto& a, const auto& b) { return a.probability < b.probability; });

  return classificationLabel(*best);
}

double durationToSeconds(const builtin_interfaces::msg::Duration& duration)
{
  return static_cast<double>(duration.sec) + static_cast<double>(duration.nanosec) * 1e-9;
}

}  // namespace

void RosMessageNormalizer::setMaxTrajectoryPoints(const std::size_t max_points)
{
  max_trajectory_points_ = std::max<std::size_t>(1, max_points);
}

sdv::ops::domain::VehicleState RosMessageNormalizer::fromVelocityReport(
  const autoware_vehicle_msgs::msg::VelocityReport& msg) const
{
  sdv::ops::domain::VehicleState state;
  state.stamp_ns = sdv::ops::domain::stampToNanoseconds(
    msg.header.stamp.sec, msg.header.stamp.nanosec);
  state.frame_id = msg.header.frame_id;
  state.longitudinal_velocity_mps = static_cast<double>(msg.longitudinal_velocity);
  state.lateral_velocity_mps = static_cast<double>(msg.lateral_velocity);
  state.heading_rate_rps = static_cast<double>(msg.heading_rate);
  return state;
}

sdv::ops::domain::EgoPose RosMessageNormalizer::fromOdometry(const nav_msgs::msg::Odometry& msg) const
{
  sdv::ops::domain::EgoPose pose;
  pose.stamp_ns = sdv::ops::domain::stampToNanoseconds(
    msg.header.stamp.sec, msg.header.stamp.nanosec);
  pose.frame_id = msg.header.frame_id;

  pose.x = msg.pose.pose.position.x;
  pose.y = msg.pose.pose.position.y;
  pose.z = msg.pose.pose.position.z;

  pose.qw = msg.pose.pose.orientation.w;
  pose.qx = msg.pose.pose.orientation.x;
  pose.qy = msg.pose.pose.orientation.y;
  pose.qz = msg.pose.pose.orientation.z;

  pose.yaw_rad = sdv::ops::domain::quaternionToYaw(pose.qw, pose.qx, pose.qy, pose.qz);
  pose.speed_mps = std::hypot(msg.twist.twist.linear.x, msg.twist.twist.linear.y);
  return pose;
}

sdv::ops::domain::Trajectory RosMessageNormalizer::fromTrajectory(
  const autoware_planning_msgs::msg::Trajectory& msg) const
{
  sdv::ops::domain::Trajectory trajectory;
  trajectory.stamp_ns = sdv::ops::domain::stampToNanoseconds(
    msg.header.stamp.sec, msg.header.stamp.nanosec);
  trajectory.frame_id = msg.header.frame_id;

  const auto& points = msg.points;
  if (points.empty()) {
    return trajectory;
  }

  const std::size_t count = std::min(points.size(), max_trajectory_points_);
  const std::size_t stride = std::max<std::size_t>(1, points.size() / count);

  trajectory.points.reserve(count);
  for (std::size_t i = 0; i < points.size(); i += stride) {
    const auto& pt = points[i];
    sdv::ops::domain::TrajectoryWaypoint wp;
    wp.x = pt.pose.position.x;
    wp.y = pt.pose.position.y;
    wp.z = pt.pose.position.z;
    wp.yaw_rad = sdv::ops::domain::quaternionToYaw(
      pt.pose.orientation.w, pt.pose.orientation.x, pt.pose.orientation.y, pt.pose.orientation.z);
    wp.longitudinal_velocity_mps = static_cast<double>(pt.longitudinal_velocity_mps);
    wp.time_from_start_sec = durationToSeconds(pt.time_from_start);
    trajectory.points.push_back(wp);
    if (trajectory.points.size() >= count) {
      break;
    }
  }

  return trajectory;
}

sdv::ops::domain::PerceptionObjectList RosMessageNormalizer::fromPredictedObjects(
  const autoware_perception_msgs::msg::PredictedObjects& msg) const
{
  sdv::ops::domain::PerceptionObjectList list;
  list.stamp_ns = sdv::ops::domain::stampToNanoseconds(
    msg.header.stamp.sec, msg.header.stamp.nanosec);
  list.frame_id = msg.header.frame_id;
  list.objects.reserve(msg.objects.size());

  for (const auto& object : msg.objects) {
    sdv::ops::domain::PerceptionObject out;
    out.object_id = uuidToHex(object.object_id);
    out.existence_probability = static_cast<double>(object.existence_probability);
    out.classification = bestClassification(object);

    const auto& pose = object.kinematics.initial_pose_with_covariance.pose;
    out.x = pose.position.x;
    out.y = pose.position.y;
    out.z = pose.position.z;
    out.yaw_rad = sdv::ops::domain::quaternionToYaw(
      pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);

    out.length_m = static_cast<double>(object.shape.dimensions.x);
    out.width_m = static_cast<double>(object.shape.dimensions.y);
    out.height_m = static_cast<double>(object.shape.dimensions.z);

    out.velocity_x_mps = static_cast<double>(object.kinematics.initial_twist_with_covariance.twist.linear.x);
    out.velocity_y_mps = static_cast<double>(object.kinematics.initial_twist_with_covariance.twist.linear.y);

    list.objects.push_back(std::move(out));
  }

  return list;
}

}  // namespace sdv::ros_bridge
