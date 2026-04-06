#pragma once

#include "LifeCycleManager/Feature.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <string>

namespace sdv::feature {

class NavigationFeature;

namespace nav_ab {
void sendVariantA(NavigationFeature& f,
    sdv::platform::communications::eventbus::EventId id,
    const std::string& payloadBase,
    const char* ctxLabel,
    bool& any,
    bool pathEnabled);
void sendVariantB(NavigationFeature& f,
    sdv::platform::communications::eventbus::EventId id,
    const std::string& payloadBase,
    const char* ctxLabel,
    bool& any,
    bool pathEnabled);
}  // namespace nav_ab

class NavigationFeature : public sdv::platform::lifecyclemgr::Feature {
    friend void nav_ab::sendVariantA(NavigationFeature&,
        sdv::platform::communications::eventbus::EventId,
        const std::string&, const char*, bool&, bool);
    friend void nav_ab::sendVariantB(NavigationFeature&,
        sdv::platform::communications::eventbus::EventId,
        const std::string&, const char*, bool&, bool);

public:
    explicit NavigationFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    // Demo pacing: keep platform alive long enough to observe control events (Shadow/Kill/Flag).
    static const int broadcastCount_ = 20;
};

}  // namespace sdv::feature
