#include "Feature/EvEnergyFeature.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

EvEnergyFeature::EvEnergyFeature(EventBusInterface& eventBus)
    : Feature("EvEnergyFeature", eventBus), battery_(85) {}

void EvEnergyFeature::execute() {
    log(LogLevel::info, "초기 배터리 잔량: " + std::to_string(battery_) + "%");
    log(LogLevel::info, "에너지 모니터링 대기 중...");

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
                log(LogLevel::info, "최종 배터리 잔량: " + std::to_string(battery_) + "%");
                return;

            default:
                break;
            }
        }
    }
}

void EvEnergyFeature::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "차량 위치 수신: " + message);
    battery_ -= 3;
    log(LogLevel::info, "배터리 잔량: " + std::to_string(battery_) +
        "% | 예상 주행 가능 거리: " + std::to_string(battery_ * 4) + "km");
    const std::string base = "ENERGY_STATE battery=" + std::to_string(battery_) +
        " rangeKm=" + std::to_string(battery_ * 4);
    bool any = false;
    if (isAbPathAEnabled()) {
        any = true;
        sendDomainEvent(EventId::EnergyStateUpdate, base + " variant=A");
    }
    if (isAbPathBEnabled()) {
        any = true;
        sendDomainEvent(EventId::EnergyStateUpdate, base + " variant=B");
    }
    if (!any) {
        log(LogLevel::warn, "A/B 경로 모두 비활성, 에너지 상태 이벤트 미전송");
    }
}

void EvEnergyFeature::processRoadInfo(const std::string& message) {
    log(LogLevel::info, "도로 정보 수신: " + message);
    if (message.find("오르막") != std::string::npos) {
        log(LogLevel::warn, "오르막 감지: 에너지 소모 증가 예상, 에코 모드 권장");
    } else if (message.find("내리막") != std::string::npos) {
        log(LogLevel::info, "내리막 감지: 회생제동 최적화 모드 활성화");
        battery_ += 1;
        log(LogLevel::info, "회생제동으로 배터리 회수: " + std::to_string(battery_) + "%");
    } else {
        log(LogLevel::info, "평지 주행: 표준 에너지 모드 유지");
    }
    const std::string base =
        "ENERGY_STATE roadHint=processed battery=" + std::to_string(battery_);
    bool any = false;
    if (isAbPathAEnabled()) {
        any = true;
        sendDomainEvent(EventId::EnergyStateUpdate, base + " variant=A");
    }
    if (isAbPathBEnabled()) {
        any = true;
        sendDomainEvent(EventId::EnergyStateUpdate, base + " variant=B");
    }
    if (!any) {
        log(LogLevel::warn, "A/B 경로 모두 비활성, 도로 기반 에너지 이벤트 미전송");
    }
}

}  // namespace sdv::feature

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::EvEnergyFeature feature(proxy);
    return feature.run();
}
