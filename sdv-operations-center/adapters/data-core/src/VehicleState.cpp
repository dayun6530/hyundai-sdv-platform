#include "sdv/ops/domain/VehicleState.hpp"

#include <cmath>

namespace sdv::ops::domain {

nlohmann::json VehicleState::toJson() const
{
  return nlohmann::json{
    {"stamp_ns", stamp_ns},
    {"frame_id", frame_id},
    {"longitudinal_velocity_mps", longitudinal_velocity_mps},
    {"lateral_velocity_mps", lateral_velocity_mps},
    {"heading_rate_rps", heading_rate_rps},
    {"speed_mps", std::hypot(longitudinal_velocity_mps, lateral_velocity_mps)},
  };
}

}  // namespace sdv::ops::domain
