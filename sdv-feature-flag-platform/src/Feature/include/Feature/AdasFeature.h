#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

// Domain output uses Feature::sendDomainEvent — Shadow mirroring is handled entirely in the base class.
// `virtual` base supports EA `Car` multiple inheritance (single shared Feature subobject).
class AdasFeature : public virtual sdv::platform::lifecyclemgr::Feature {
public:
    explicit AdasFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    virtual void runAdasMainLoop();

private:
    void processVehicleLocation(const std::string& message);
    void processRoadInfo(const std::string& message);
};

}  // namespace sdv::feature
