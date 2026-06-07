#include "sdv/ops/domain/Trajectory.hpp"

namespace sdv::ops::domain {

nlohmann::json Trajectory::toJson() const
{
  nlohmann::json waypoints = nlohmann::json::array();
  for (const auto& p : points) {
    waypoints.push_back({
      {"x", p.x},
      {"y", p.y},
      {"z", p.z},
      {"yaw_rad", p.yaw_rad},
      {"longitudinal_velocity_mps", p.longitudinal_velocity_mps},
      {"time_from_start_sec", p.time_from_start_sec},
    });
  }

  return nlohmann::json{
    {"stamp_ns", stamp_ns},
    {"frame_id", frame_id},
    {"point_count", points.size()},
    {"points", std::move(waypoints)},
  };
}

}  // namespace sdv::ops::domain
