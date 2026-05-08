#pragma once

#include "LifeCycleManager/Feature.h"
#include "FeatureCommunication/EventBus/EventBusInterface.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <string>

namespace sdv::platform::lifecyclemgr {

class FeatureEvaluator : public Feature {
public:
    static constexpr const char* defaultInboundPath = "logs/SignalsInbound.cmd";
    static constexpr const char* defaultOutboundPath = "logs/SignalsOutbound.cmd";

    FeatureEvaluator(EventBusInterface& eventBus,
                     const std::string& inboundPath = defaultInboundPath,
                     const std::string& outboundPath = defaultOutboundPath);

protected:
    void execute() override;

private:
    void processSignals(); // legacy file input disabled (kept for API compatibility)
    /// After startup delay: turn on Shadow mirroring for every Primary that has a Shadow pair (car-line + legacy).
    void emitAutoShadowModeForAllPrimaries();
    void broadcastKillSwitchToAllTargetFeatures();
    bool sendFlagOn(const std::string& featureName);
    bool sendFlagOff(const std::string& featureName);
    bool sendKillSwitch(const std::string& featureName);
    bool sendShadowOn(const std::string& primaryName);
    bool sendShadowOff(const std::string& primaryName);
    bool sendShadowPromotionRequest(const std::string& primaryLogicalName);
    bool sendAbSet(const std::string& featureName, const std::string& variant);
    void sendWarning(const std::string& target, const std::string& message);

    std::string inboundPath_;
    std::string outboundPath_;

    // File-based inbound/outbound signals are deprecated in the MQTT-only flow.
    // Keep the fields for backward-compatible logging only.
    bool fileSignalsEnabled_{false};
};

}  // namespace sdv::platform::lifecyclemgr
