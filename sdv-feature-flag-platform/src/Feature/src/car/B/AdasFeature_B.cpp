#include "Feature/car/B/AdasFeature_B.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

AdasFeature_B::AdasFeature_B(EventBusInterface& eventBus)
    : Feature("AdasFeature_B", eventBus)
    , Car(eventBus) {}

void AdasFeature_B::execute() {
    const std::string line = getAbVariant().value_or("B");
    log(LogLevel::info,
        std::string("Car platform — AdasFeature_B (B-line / Variant=") + line + ")");
    log(LogLevel::info, "[ADAS-B] lane keeping assist active");
    log(LogLevel::info, "[ADAS-B] forward collision warning sensitivity: high");
    log(LogLevel::info, "[ADAS-B] enhanced object detection enabled");
    log(LogLevel::info, "자율주행 대기 중... (B-line / Genesis ADAS profile)");
    runAdasMainLoop();
}

void AdasFeature_B::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "현재 위치 수신 (Genesis): " + message);
    if (flag_.isSubFeatureEnabled("FCA")) {
        log(LogLevel::info, "FCA: 프리미엄 전방 융합 — 충돌 위험 평가");
    } else {
        log(LogLevel::info, "FCA: OFF (전방충돌방지 비활성)");
    }
    if (flag_.isSubFeatureEnabled("LKA")) {
        log(LogLevel::info, "LKA: HDA 차선 유지 보조");
    } else {
        log(LogLevel::info, "LKA: OFF");
    }
    if (flag_.isSubFeatureEnabled("SCC")) {
        log(LogLevel::info, "SCC: 스마트 크루즈 (Genesis 프로파일)");
    } else {
        log(LogLevel::info, "SCC: OFF");
    }
    log(LogLevel::info, "주변 차량/장애물 감지 — 프리미엄 센서 융합");
    sendDomainEvent(EventId::AdasDecisionUpdate,
        std::string("ADAS_DECISION line=B source=location status=tracking|") + message);
}

void AdasFeature_B::processRoadInfo(const std::string& message) {
    log(LogLevel::info, "도로 정보 수신 (Genesis): " + message);
    log(LogLevel::info, "HDA 경로·속도 계획 반영 (Genesis)");
    if (flag_.isSubFeatureEnabled("FCA")) {
        log(LogLevel::info, "FCA: 위험 구간 HDA 감속 연계");
    } else {
        log(LogLevel::info, "FCA: OFF — 전방 위험 평가 생략");
    }
    if (flag_.isSubFeatureEnabled("LKA")) {
        log(LogLevel::info, "LKA: HDA 차선 궤적 (도로 기준)");
    } else {
        log(LogLevel::info, "LKA: OFF");
    }
    if (flag_.isSubFeatureEnabled("SCC")) {
        log(LogLevel::info, "SCC: 도로 속도 프로파일 추종");
    } else {
        log(LogLevel::info, "SCC: OFF");
    }
    log(LogLevel::info, "프리미엄 자율주행 추종 중...");
    sendDomainEvent(EventId::AdasDecisionUpdate,
        std::string("ADAS_DECISION line=B source=road status=planning|") + message);
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
