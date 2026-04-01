#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class NavigationFeature : public sdv::platform::lifecyclemgr::Feature {
public:
    explicit NavigationFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    // Demo pacing: keep platform alive long enough to observe control events (Shadow/Kill/Flag).
    static const int broadcastCount_ = 20;
};

}  // namespace sdv::feature
