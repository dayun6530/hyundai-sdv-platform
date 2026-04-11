#include "Feature/car/B/EvEnergyFeature_B.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

EvEnergyFeature_B::EvEnergyFeature_B(EventBusInterface& eventBus)
    : Feature("EvEnergyFeature_B", eventBus)
    , Car(eventBus) {}

void EvEnergyFeature_B::execute() {
    const char line = getAbVariant().value_or('B');
    log(LogLevel::info,
        std::string("Car platform — EvEnergyFeature_B (B-line / Flag A|B=") + line + ")");
    log(LogLevel::info, "B-line 에너지 스택 — 대용량 배터리 / 800V 프로파일 (데모)");
    log(LogLevel::info, "초기 배터리 잔량: " + std::to_string(battery_) + "%");
    log(LogLevel::info, "에너지 모니터링 대기 중... (Genesis)");
    runEvEnergyMainLoop();
}

void EvEnergyFeature_B::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "차량 위치 수신 (Genesis energy): " + message);
    battery_ -= 2;
    log(LogLevel::info, "배터리 잔량: " + std::to_string(battery_) +
        "% | 예상 주행 가능 거리: " + std::to_string(battery_ * 5) + "km");
    sendDomainEvent(EventId::EnergyStateUpdate,
        std::string("ENERGY_STATE line=B battery=") + std::to_string(battery_) +
        " rangeKm=" + std::to_string(battery_ * 5) + "|SHADOW_LOC|" + message);
}

void EvEnergyFeature_B::processRoadInfo(const std::string& message) {
    const std::string& road = message;
    log(LogLevel::info, "도로 정보 수신 (Genesis energy): " + road);
    if (road.find("오르막") != std::string::npos) {
        log(LogLevel::warn, "오르막: B-line 에코+파워 한계 보호");
    } else if (road.find("내리막") != std::string::npos) {
        battery_ += 2;
        log(LogLevel::info, "회생제동 (고출력): " + std::to_string(battery_) + "%");
    } else {
        log(LogLevel::info, "평지: 컴포트 주행 모드");
    }
    sendDomainEvent(EventId::EnergyStateUpdate,
        std::string("ENERGY_STATE line=B roadHint=processed battery=") +
        std::to_string(battery_) + "|SHADOW_ROAD|" + road);
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
    sdv::feature::EvEnergyFeature_B feature(proxy);
    sdv::platform::lifecyclemgr::Feature::applyLaunchAbVariantFromArgv(argc, argv, feature.getFlag());
    return feature.run();
}
