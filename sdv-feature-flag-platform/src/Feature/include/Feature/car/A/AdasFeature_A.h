#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class AdasFeature_A final : public Car {
public:
    explicit AdasFeature_A(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
};

}  // namespace sdv::feature
