#include "LifeCycleManager/Feature.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <signal.h>
#include <string>
#include <cctype>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

#ifndef SHADOW_NAME
#error "SHADOW_NAME must be defined (e.g., NavigationFeatureShadow)"
#endif

#ifndef PRIMARY_NAME
#error "PRIMARY_NAME must be defined (e.g., NavigationFeature)"
#endif

namespace sdv::feature {

static const char* eventIdToString(EventId id) {
    switch (id) {
    case EventId::VehicleLocationUpdate: return "VehicleLocationUpdate";
    case EventId::RoadInfoUpdate: return "RoadInfoUpdate";
    case EventId::NavigationGuidanceUpdate: return "NavigationGuidanceUpdate";
    case EventId::AdasDecisionUpdate: return "AdasDecisionUpdate";
    case EventId::EnergyStateUpdate: return "EnergyStateUpdate";
    case EventId::FeatureFlagOn: return "FeatureFlagOn";
    case EventId::FeatureFlagOff: return "FeatureFlagOff";
    case EventId::KillSwitch: return "KillSwitch";
    case EventId::ShadowMirrorData: return "ShadowMirrorData";
    case EventId::ShadowModeOn: return "ShadowModeOn";
    case EventId::ShadowModeOff: return "ShadowModeOff";
    case EventId::QuitTheSystem: return "QuitTheSystem";
    default: return "Unknown";
    }
}

static bool tryParseInt32(const std::string& s, int32_t& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') return false;
    }
    try {
        long v = std::stol(s);
        if (v < INT32_MIN || v > INT32_MAX) return false;
        out = static_cast<int32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

class ShadowReceiverFeature : public Feature {
public:
    explicit ShadowReceiverFeature(EventBusInterface& eventBus)
        : Feature(SHADOW_NAME, eventBus) {
        primaryName_ = PRIMARY_NAME;
    }

protected:
    void execute() override {
        log(LogLevel::info, std::string("Shadow started. Primary: ") + primaryName_);

        while (!isStopRequested()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto events = pollEvents();
            for (const auto& event : events) {
                if (event.getEventId() != EventId::ShadowMirrorData) continue;

                // Format: MIRROR|<original_event_id_decimal>|<payload>
                const std::string& msg = event.getMessage();
                const std::string prefix = "MIRROR|";
                if (msg.rfind(prefix, 0) != 0) continue;

                auto p1 = msg.find('|', prefix.size());
                if (p1 == std::string::npos) continue;

                std::string idStr = msg.substr(prefix.size(), p1 - prefix.size());
                std::string payload = msg.substr(p1 + 1);

                // Payload is prefixed with "<PrimaryName>:<original_payload>"
                // so that each Shadow process can filter its own stream.
                std::string expectedPrefix = primaryName_ + ":";
                if (payload.rfind(expectedPrefix, 0) != 0) {
                    continue;
                }
                payload = payload.substr(expectedPrefix.size());

                int32_t originalIdInt = 0;
                if (!tryParseInt32(idStr, originalIdInt)) {
                    log(LogLevel::warn, "mirror parse failed: invalid original event id: " + idStr);
                    continue;
                }

                EventId originalId = static_cast<EventId>(originalIdInt);

                // Shadow must not send external-impact events. Only internal processing/logging here.
                switch (originalId) {
                case EventId::VehicleLocationUpdate:
                case EventId::RoadInfoUpdate:
                case EventId::NavigationGuidanceUpdate:
                case EventId::AdasDecisionUpdate:
                case EventId::EnergyStateUpdate:
                    log(LogLevel::info,
                        std::string("mirror received: ") + eventIdToString(originalId) +
                        " payload=[" + payload + "]");
                    break;
                default:
                    log(LogLevel::warn,
                        std::string("mirror received: UNKNOWN(") + std::to_string(originalIdInt) +
                        ") payload=[" + payload + "]");
                    break;
                }
            }
        }
    }
};

}  // namespace sdv::feature

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::ShadowReceiverFeature feature(proxy);
    return feature.run();
}

