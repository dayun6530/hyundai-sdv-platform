#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

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
};

}  // namespace sdv::feature
