#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class EvEnergyFeature_A final : public Car {
public:
    explicit EvEnergyFeature_A(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
};

}  // namespace sdv::feature
