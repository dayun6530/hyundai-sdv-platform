#include "Feature/car/Car.h"

namespace sdv::feature {

Car::Car(sdv::platform::communications::eventbus::EventBusInterface& eventBus)
    : AdasFeature(eventBus)
    , NavigationFeature(eventBus)
    , EvEnergyFeature(eventBus) {}

}  // namespace sdv::feature
