#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sdv::ops::domain {

struct PerceptionObject {
  std::string object_id;
  std::string classification;
  double existence_probability{0.0};

  double x{0.0};
  double y{0.0};
  double z{0.0};
  double yaw_rad{0.0};

  double length_m{0.0};
  double width_m{0.0};
  double height_m{0.0};

  double velocity_x_mps{0.0};
  double velocity_y_mps{0.0};
};

struct PerceptionObjectList {
  int64_t stamp_ns{0};
  std::string frame_id;
  std::vector<PerceptionObject> objects;

  nlohmann::json toJson() const;
};

}  // namespace sdv::ops::domain
