#pragma once

#include "Feature/AdasFeature.h"
#include "Feature/EvEnergyFeature.h"
#include "Feature/NavigationFeature.h"

namespace sdv::feature {

/// EA vehicle platform node: inherits Adas + Navigation + Energy domain bases with a single
/// \ref sdv::platform::lifecyclemgr::Feature subobject (virtual inheritance).
class Car : public AdasFeature, public NavigationFeature, public EvEnergyFeature {
protected:
    explicit Car(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

    void execute() override = 0;
};

}  // namespace sdv::feature
