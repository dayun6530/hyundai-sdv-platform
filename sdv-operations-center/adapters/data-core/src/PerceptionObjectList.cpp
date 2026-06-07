#include "sdv/ops/domain/PerceptionObjectList.hpp"

namespace sdv::ops::domain {

nlohmann::json PerceptionObjectList::toJson() const
{
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& o : objects) {
    arr.push_back({
      {"object_id", o.object_id},
      {"classification", o.classification},
      {"existence_probability", o.existence_probability},
      {"position", {{"x", o.x}, {"y", o.y}, {"z", o.z}}},
      {"yaw_rad", o.yaw_rad},
      {"dimensions_m", {{"length", o.length_m}, {"width", o.width_m}, {"height", o.height_m}}},
      {"velocity_mps", {{"x", o.velocity_x_mps}, {"y", o.velocity_y_mps}}},
    });
  }

  return nlohmann::json{
    {"stamp_ns", stamp_ns},
    {"frame_id", frame_id},
    {"object_count", objects.size()},
    {"objects", std::move(arr)},
  };
}

}  // namespace sdv::ops::domain
