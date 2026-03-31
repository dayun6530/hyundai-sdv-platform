#pragma once

#include <cstdint>

namespace sdv::platform::communications::eventbus {

enum class EventId : int32_t {
    VehicleLocationUpdate = 1,
    RoadInfoUpdate = 2,

    FeatureFlagOn = 10,
    FeatureFlagOff = 11,
    KillSwitch = 12,

    QuitTheSystem = 99
};

}  // namespace sdv::platform::communications::eventbus
