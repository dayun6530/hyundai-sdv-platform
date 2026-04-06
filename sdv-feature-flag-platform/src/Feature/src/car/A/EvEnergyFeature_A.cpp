#include "Feature/car/A/EvEnergyFeature_A.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

EvEnergyFeature_A::EvEnergyFeature_A(EventBusInterface& eventBus)
    : Feature("EvEnergyFeature_A", eventBus)
    , Car(eventBus) {}

void EvEnergyFeature_A::execute() {
    const char line = getAbVariant().value_or('A');
    log(LogLevel::info,
        std::string("Car platform — EvEnergyFeature_A (A-line / Flag A|B=") + line + ")");
    runEvEnergyMainLoop();
}

}  // namespace sdv::feature

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) {
        address = argv[1];
    }

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::EvEnergyFeature_A feature(proxy);
    sdv::platform::lifecyclemgr::Feature::applyLaunchAbVariantFromArgv(argc, argv, feature.getFlag());
    return feature.run();
}
