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
    /// Emitted by the target feature process when it accepts a KillSwitch and begins stopping.
    /// Message format: "<feature_name>|ACK"
    KillSwitchAck = 13,

    // Shadow Mode + A/B
    ShadowMirrorData = 20,
    ShadowModeOn = 21,
    ShadowModeOff = 22,
    AbVariantAssign = 23,
    /// Orchestrator / FeatureEvaluator: request Shadow→Primary handover (see Feature::tryConsumeAsPlatformControl).
    ShadowPromotionRequest = 24,

    /// Telemetry payload (JSON). Intended for DataCollector / exporters (not control-plane).
    TelemetryData = 30,

    QuitTheSystem = 99
};

}  // namespace sdv::platform::communications::eventbus
