#pragma once

#include "carla/integration/CarlaTypes.h"
#include "ops/domain/OpsTypes.h"

namespace sdv::dash::carla::integration {

/** Generates lane centerline, boundaries, and drivable corridor as a trajectory polyline. */
class LaneExtractor {
public:
    static sdv::dash::ops::Trajectory extract(const CarlaWorldSnapshot& snapshot);
};

}  // namespace sdv::dash::carla::integration
