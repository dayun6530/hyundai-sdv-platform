#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class AdasFeature : public sdv::platform::lifecyclemgr::Feature {
public:
    explicit AdasFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    void processVehicleLocation(const std::string& message);
    void processRoadInfo(const std::string& message);
};

}  // namespace sdv::feature
