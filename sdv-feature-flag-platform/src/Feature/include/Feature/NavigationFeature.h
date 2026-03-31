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
    static const int broadcastCount_ = 5;
};

}  // namespace sdv::feature
