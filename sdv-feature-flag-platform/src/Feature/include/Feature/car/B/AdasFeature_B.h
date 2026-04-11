#pragma once

#include "Feature/car/Car.h"

namespace sdv::feature {

class AdasFeature_B final : public Car {
public:
    explicit AdasFeature_B(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    void processVehicleLocation(const std::string& message) override;
    void processRoadInfo(const std::string& message) override;
};

}  // namespace sdv::feature
