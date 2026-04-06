#include "Feature/car/A/NavigationFeature_A.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

NavigationFeature_A::NavigationFeature_A(EventBusInterface& eventBus)
    : Feature("NavigationFeature_A", eventBus)
    , Car(eventBus) {}

void NavigationFeature_A::execute() {
    const char line = getAbVariant().value_or('A');
    log(LogLevel::info,
        std::string("Car platform — NavigationFeature_A (A-line / Flag A|B=") + line + ")");
    runNavigationMainLoop();
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
    sdv::feature::NavigationFeature_A feature(proxy);
    sdv::platform::lifecyclemgr::Feature::applyLaunchAbVariantFromArgv(argc, argv, feature.getFlag());
    return feature.run();
}
