#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class EvEnergyFeature : public virtual sdv::platform::lifecyclemgr::Feature {
public:
    explicit EvEnergyFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    virtual void runEvEnergyMainLoop();

protected:
    int battery_;

private:
    void processVehicleLocation(const std::string& message);
    void processRoadInfo(const std::string& message);
};

}  // namespace sdv::feature
