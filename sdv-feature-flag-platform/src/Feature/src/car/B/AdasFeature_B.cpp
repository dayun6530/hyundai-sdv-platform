#include "Feature/car/B/AdasFeature_B.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <chrono>
#include <signal.h>
#include <string>
#include <thread>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

AdasFeature_B::AdasFeature_B(EventBusInterface& eventBus)
    : Feature("AdasFeature_B", eventBus)
    , Car(eventBus) {}

void AdasFeature_B::execute() {
    const char line = getAbVariant().value_or('B');
    log(LogLevel::info,
        std::string("Car platform — AdasFeature_B (B-line / Flag A|B=") + line + ")");
    runAdasMainLoop();
}

void AdasFeature_B::runAdasMainLoop() {
    log(LogLevel::info, "자율주행 대기 중... (B-line / Genesis ADAS profile)");

    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto events = pollEvents();
        for (const auto& event : events) {
            switch (event.getEventId()) {
            case EventId::VehicleLocationUpdate:
                log(LogLevel::info, "현재 위치 수신 (Genesis): " + event.getMessage());
                log(LogLevel::info, "주변 차량/장애물 감지 — 프리미엄 센서 융합");
                sendDomainEvent(EventId::AdasDecisionUpdate,
                    "ADAS_DECISION line=B source=location status=tracking");
                break;

            case EventId::RoadInfoUpdate:
                log(LogLevel::info, "도로 정보 수신 (Genesis): " + event.getMessage());
                sendDomainEvent(EventId::AdasDecisionUpdate,
                    "ADAS_DECISION line=B source=road status=planning");
                break;

            case EventId::QuitTheSystem:
                log(LogLevel::info, "종료 신호 수신");
                return;

            default:
                break;
            }
        }
    }
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
    sdv::feature::AdasFeature_B feature(proxy);
    sdv::platform::lifecyclemgr::Feature::applyLaunchAbVariantFromArgv(argc, argv, feature.getFlag());
    return feature.run();
}
