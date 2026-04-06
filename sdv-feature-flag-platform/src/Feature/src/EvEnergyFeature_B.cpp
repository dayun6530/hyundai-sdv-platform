#include "Feature/EvEnergyFeature.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace sdv::feature::energy_ab {

void sendLocationStateB(EvEnergyFeature& f, const std::string& basePayload) {
    f.sendDomainEvent(sdv::platform::communications::eventbus::EventId::EnergyStateUpdate,
        basePayload + " variant=B");
}

void sendRoadStateB(EvEnergyFeature& f, const std::string& basePayload) {
    f.sendDomainEvent(sdv::platform::communications::eventbus::EventId::EnergyStateUpdate,
        basePayload + " variant=B");
}

}  // namespace sdv::feature::energy_ab
