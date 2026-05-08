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
    const std::string line = getAbVariant().value_or("B");
    log(LogLevel::info,
        std::string("Car platform — EvEnergyFeature_B (B-line / Variant=") + line + ")");
    log(LogLevel::info, "[ENERGY-B] battery state: 82%");
    log(LogLevel::info, "[ENERGY-B] regenerative braking: optimized");
    log(LogLevel::info, "[ENERGY-B] eco range prediction enabled");
    log(LogLevel::info, "B-line 에너지 스택 — 대용량 배터리 / 800V 프로파일 (데모)");
    log(LogLevel::info, "초기 배터리 잔량: " + std::to_string(battery_) + "%");
    log(LogLevel::info, "에너지 모니터링 대기 중... (Genesis)");
    runEvEnergyMainLoop();
}

void EvEnergyFeature_B::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "차량 위치 수신 (Genesis energy): " + message);
    battery_ -= 2;
    if (flag_.isSubFeatureEnabled("RANGE")) {
        log(LogLevel::info, "RANGE (B): 예상 거리 — " + std::to_string(battery_ * 5) + "km");
    } else {
        log(LogLevel::info, "RANGE: OFF");
    }
    if (flag_.isSubFeatureEnabled("ECO")) {
        log(LogLevel::info, "ECO (B): 800V 스택 효율 힌트");
    } else {
        log(LogLevel::info, "ECO: OFF");
    }
    if (flag_.isSubFeatureEnabled("REGEN")) {
        log(LogLevel::info, "REGEN (B): 위치 기반 회생 힌트");
    } else {
        log(LogLevel::info, "REGEN: OFF");
    }
    sendDomainEvent(EventId::EnergyStateUpdate,
        std::string("ENERGY_STATE line=B battery=") + std::to_string(battery_) +
        " rangeKm=" + std::to_string(battery_ * 5) + "|SHADOW_LOC|" + message);
}

void EvEnergyFeature_B::processRoadInfo(const std::string& message) {
    const std::string& road = message;
    log(LogLevel::info, "도로 정보 수신 (Genesis energy): " + road);
    if (road.find("오르막") != std::string::npos) {
        if (flag_.isSubFeatureEnabled("ECO")) {
            log(LogLevel::warn, "오르막: B-line 에코+파워 한계 보호");
        } else {
            log(LogLevel::info, "오르막 (ECO OFF)");
        }
    } else if (road.find("내리막") != std::string::npos) {
        if (flag_.isSubFeatureEnabled("REGEN")) {
            battery_ += 2;
            log(LogLevel::info, "회생제동 (고출력): " + std::to_string(battery_) + "%");
        } else {
            log(LogLevel::info, "REGEN: OFF — 회생 보정 생략");
        }
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
