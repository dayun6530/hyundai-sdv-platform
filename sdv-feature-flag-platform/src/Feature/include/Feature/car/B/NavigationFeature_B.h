#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class NavigationFeature_B final : public Car {
public:
    explicit NavigationFeature_B(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    void runNavigationMainLoop() override;
};

}  // namespace sdv::feature
