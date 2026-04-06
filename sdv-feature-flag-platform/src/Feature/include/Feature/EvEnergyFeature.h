#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class EvEnergyFeature;

namespace energy_ab {
void sendLocationStateA(EvEnergyFeature& f, const std::string& basePayload);
void sendLocationStateB(EvEnergyFeature& f, const std::string& basePayload);
void sendRoadStateA(EvEnergyFeature& f, const std::string& basePayload);
void sendRoadStateB(EvEnergyFeature& f, const std::string& basePayload);
}  // namespace energy_ab

class EvEnergyFeature : public sdv::platform::lifecyclemgr::Feature {
    friend void energy_ab::sendLocationStateA(EvEnergyFeature&, const std::string&);
    friend void energy_ab::sendLocationStateB(EvEnergyFeature&, const std::string&);
    friend void energy_ab::sendRoadStateA(EvEnergyFeature&, const std::string&);
    friend void energy_ab::sendRoadStateB(EvEnergyFeature&, const std::string&);

public:
    explicit EvEnergyFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    void processVehicleLocation(const std::string& message);
    void processRoadInfo(const std::string& message);

    int battery_;
};

}  // namespace sdv::feature
