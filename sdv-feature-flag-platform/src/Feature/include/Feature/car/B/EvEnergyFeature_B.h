#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class EvEnergyFeature_B final : public Car {
public:
    explicit EvEnergyFeature_B(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    void runEvEnergyMainLoop() override;
};

}  // namespace sdv::feature
