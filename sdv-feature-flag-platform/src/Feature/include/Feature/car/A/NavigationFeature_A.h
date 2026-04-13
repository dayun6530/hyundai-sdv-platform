#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class NavigationFeature_A final : public Car {
public:
    explicit NavigationFeature_A(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    std::string navGuidanceForIteration(int iteration1Based) override;
};

}  // namespace sdv::feature
