#include "Feature/AdasFeature.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <signal.h>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

AdasFeature::AdasFeature(EventBusInterface& eventBus)
    : Feature("AdasFeature", eventBus) {}

void AdasFeature::execute() {
    log(LogLevel::info, "자율주행 대기 중...");

    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto events = pollEvents();
        for (const auto& event : events) {
            switch (event.getEventId()) {
            case EventId::VehicleLocationUpdate:
                processVehicleLocation(event.getMessage());
                break;

            case EventId::RoadInfoUpdate:
                processRoadInfo(event.getMessage());
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

void AdasFeature::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "현재 위치 수신: " + message);
    log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");

    bool any = false;

    if (isAbPathAEnabled()) {
        any = true;
        adas_ab::onLocationA(*this, message);
    }

    if (isAbPathBEnabled()) {
        any = true;
        adas_ab::onLocationB(*this, message);
    }

    if (!any) {
        log(LogLevel::warn, "A/B 경로 모두 비활성, 위치 기반 ADAS 이벤트 미전송");
    }
}

void AdasFeature::processRoadInfo(const std::string& message) {
    log(LogLevel::info, "도로 정보 수신: " + message);
    log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
    log(LogLevel::info, "자율주행 중...");

    bool any = false;

    if (isAbPathAEnabled()) {
        any = true;
        adas_ab::onRoadA(*this, message);
    }

    if (isAbPathBEnabled()) {
        any = true;
        adas_ab::onRoadB(*this, message);
    }

    if (!any) {
        log(LogLevel::warn, "A/B 경로 모두 비활성, 도로 기반 ADAS 이벤트 미전송");
    }
}

}  // namespace sdv::feature

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::AdasFeature feature(proxy);
    return feature.run();
}