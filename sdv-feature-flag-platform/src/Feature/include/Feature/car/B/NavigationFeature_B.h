#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class NavigationFeature_B final : public Car {
public:
    explicit NavigationFeature_B(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    int navDemoPointCount() override;
    const char* navDemoLocation(int rMod) override;
    const char* navDemoRoad(int rMod) override;
    void navLogIterationBanner(int iteration1Based) override;
    std::string navGuidanceForIteration(int iteration1Based) override;
    void navLogRunComplete() override;
};

}  // namespace sdv::feature
