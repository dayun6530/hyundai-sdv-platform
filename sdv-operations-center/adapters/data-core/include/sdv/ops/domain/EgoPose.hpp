#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace sdv::ops::domain {

struct EgoPose {
  int64_t stamp_ns{0};
  std::string frame_id;

  double x{0.0};
  double y{0.0};
  double z{0.0};

  double qw{1.0};
  double qx{0.0};
  double qy{0.0};
  double qz{0.0};

  /// Yaw derived from quaternion (radians, map frame).
  double yaw_rad{0.0};
  double speed_mps{0.0};

  nlohmann::json toJson() const;
};

}  // namespace sdv::ops::domain
