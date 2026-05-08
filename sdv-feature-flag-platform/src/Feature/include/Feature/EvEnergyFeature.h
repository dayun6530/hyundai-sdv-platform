#pragma once

#include "LifeCycleManager/Feature.h"
#include "Feature/SubFeatures/SubFeatureTree.h"

#include <string>
#include <memory>

namespace sdv::feature {

class EvEnergyFeature : public virtual sdv::platform::lifecyclemgr::Feature {
public:
    explicit EvEnergyFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    std::string distributionVersionLabelForLog() const override;
    virtual void runEvEnergyMainLoop();

    int battery_;

    virtual void processVehicleLocation(const std::string& message);
    virtual void processRoadInfo(const std::string& message);

private:
    std::unique_ptr<sdv::feature::sub::CompositeNode> subTree_;
};

}  // namespace sdv::feature
