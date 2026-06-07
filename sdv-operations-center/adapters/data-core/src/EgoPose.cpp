#include "sdv/ops/domain/EgoPose.hpp"

#include "sdv/ops/domain/TimeConv.hpp"

namespace sdv::ops::domain {

nlohmann::json EgoPose::toJson() const
{
  return nlohmann::json{
    {"stamp_ns", stamp_ns},
    {"frame_id", frame_id},
    {"position", {{"x", x}, {"y", y}, {"z", z}}},
    {"orientation", {{"w", qw}, {"x", qx}, {"y", qy}, {"z", qz}}},
    {"yaw_rad", yaw_rad},
    {"speed_mps", speed_mps},
    {"stamp_iso", nanosecondsToIso8601(stamp_ns)},
  };
}

}  // namespace sdv::ops::domain
