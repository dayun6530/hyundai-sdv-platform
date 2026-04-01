#pragma once

#include <cstdint>

namespace sdv::platform::communications::eventbus {

enum class EventId : int32_t {
    VehicleLocationUpdate = 1,
    RoadInfoUpdate = 2,
    NavigationGuidanceUpdate = 3,
    AdasDecisionUpdate = 4,
    EnergyStateUpdate = 5,

    FeatureFlagOn = 10,
    FeatureFlagOff = 11,
    KillSwitch = 12,

<<<<<<< Updated upstream
    // Shadow Mode
    ShadowMirrorData = 20,
    ShadowModeOn = 21,
    ShadowModeOff = 22,
=======
    ShadowMirrorData = 20,
    ShadowModeOn = 21,
    ShadowModeOff = 22,
    AbVariantAssign = 23,
>>>>>>> Stashed changes

    QuitTheSystem = 99
};

}  // namespace sdv::platform::communications::eventbus
