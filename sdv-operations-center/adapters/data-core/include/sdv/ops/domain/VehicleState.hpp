#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace sdv::ops::domain {

struct VehicleState {
  int64_t stamp_ns{0};
  std::string frame_id;

  double longitudinal_velocity_mps{0.0};
  double lateral_velocity_mps{0.0};
  double heading_rate_rps{0.0};

  nlohmann::json toJson() const;
};

}  // namespace sdv::ops::domain
