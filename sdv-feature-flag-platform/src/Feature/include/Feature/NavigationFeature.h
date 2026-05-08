#pragma once

#include "LifeCycleManager/Feature.h"
#include "Feature/SubFeatures/SubFeatureTree.h"

#include <string>
#include <memory>

namespace sdv::feature {

class NavigationFeature : public virtual sdv::platform::lifecyclemgr::Feature {
public:
    explicit NavigationFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    std::string distributionVersionLabelForLog() const override;
    virtual void runNavigationMainLoop();

    /// B-line overrides customize demo route text; loop pacing matches A (shared implementation).
    virtual int navDemoPointCount();
    virtual const char* navDemoLocation(int rMod);
    virtual const char* navDemoRoad(int rMod);
    virtual void navLogIterationBanner(int iteration1Based);
    virtual std::string navGuidanceForIteration(int iteration1Based);
    virtual void navLogRunComplete();

private:
    // Demo pacing: iterations per "라운드" inside runNavigationMainLoop (outer execute() repeats forever for car_*).
    static const int broadcastCount_ = 50;
    std::unique_ptr<sdv::feature::sub::CompositeNode> subTree_;
    std::shared_ptr<std::string> locPayload_;
    std::shared_ptr<std::string> roadPayload_;
    std::shared_ptr<std::string> guidancePayload_;
};

}  // namespace sdv::feature
