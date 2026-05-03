#include "Feature/car/Car.h"

namespace sdv::feature {

Car::Car(sdv::platform::communications::eventbus::EventBusInterface& eventBus)
    : AdasFeature(eventBus)
    , NavigationFeature(eventBus)
    , EvEnergyFeature(eventBus) {}

std::string Car::distributionVersionLabelForLog() const {
    return AdasFeature::distributionVersionLabelForLog();
}

}  // namespace sdv::feature
