#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class AdasFeature_B final : public Car {
public:
    explicit AdasFeature_B(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    void runAdasMainLoop() override;
};

}  // namespace sdv::feature
