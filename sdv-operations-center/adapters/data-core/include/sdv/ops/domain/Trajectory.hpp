#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sdv::ops::domain {

struct TrajectoryWaypoint {
  double x{0.0};
  double y{0.0};
  double z{0.0};
  double yaw_rad{0.0};
  double longitudinal_velocity_mps{0.0};
  double time_from_start_sec{0.0};
};

struct Trajectory {
  int64_t stamp_ns{0};
  std::string frame_id;
  std::vector<TrajectoryWaypoint> points;

  nlohmann::json toJson() const;
};

}  // namespace sdv::ops::domain
