#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class NavigationFeature : public virtual sdv::platform::lifecyclemgr::Feature {
public:
    explicit NavigationFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    virtual void runNavigationMainLoop();

private:
    // Demo pacing: longer run so platform control events can be observed during tests.
    static const int broadcastCount_ = 20;
};

}  // namespace sdv::feature
