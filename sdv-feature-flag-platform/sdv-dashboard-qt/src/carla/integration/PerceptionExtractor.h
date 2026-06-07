#pragma once

#include "carla/integration/CarlaTypes.h"
#include "ops/domain/OpsTypes.h"

namespace sdv::dash::carla::integration {

class PerceptionExtractor {
public:
    static sdv::dash::ops::PerceptionObjectList extract(const CarlaWorldSnapshot& snapshot);
};

}  // namespace sdv::dash::carla::integration
