#include "Feature/EvEnergyFeature.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace sdv::feature::energy_ab {

void sendLocationStateA(EvEnergyFeature& f, const std::string& basePayload) {
    f.sendDomainEvent(sdv::platform::communications::eventbus::EventId::EnergyStateUpdate,
        basePayload + " variant=A");
}

void sendRoadStateA(EvEnergyFeature& f, const std::string& basePayload) {
    f.sendDomainEvent(sdv::platform::communications::eventbus::EventId::EnergyStateUpdate,
        basePayload + " variant=A");
}

}  // namespace sdv::feature::energy_ab
