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

    /// B-line overrides customize demo route text; loop pacing matches A (shared implementation).
    virtual int navDemoPointCount();
    virtual const char* navDemoLocation(int rMod);
    virtual const char* navDemoRoad(int rMod);
    virtual void navLogIterationBanner(int iteration1Based);
    virtual std::string navGuidanceForIteration(int iteration1Based);
    virtual void navLogRunComplete();

private:
    // Demo pacing: longer run so platform control events can be observed during tests.
    static const int broadcastCount_ = 20;
};

}  // namespace sdv::feature
