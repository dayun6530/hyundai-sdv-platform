#include "Feature/car/B/NavigationFeature_B.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <chrono>
#include <signal.h>
#include <string>
#include <thread>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

namespace {

static const char* kGenesisLocations[] = {
    "서울 강남구 봉은사로 (Genesis HQ 구간)",
    "제네시스 전용 HDA 구간 (경부선)",
    "프리미엄 라운지 연계 주차장 진입로",
    "스마트시티 파일럿 구역 (실험 노선)",
    "리무진 모드 종료 구간 (일반 도로 복귀)"
};

static const char* kGenesisRoadInfos[] = {
    "저속 크루즈 + 정밀 차선 유지 (제한속도 50km/h)",
    "HDA 2 구간 — 장거리 직선 위주 (제한속도 100km/h)",
    "실내 주차장 — UWB 기반 위치 보정",
    "실험 구간 — V2X 협력 주행 힌트 활성",
    "컴포트 서스펜션 프로파일 + 부드러운 감속"
};

static constexpr int kGenesisRoutePointCount =
    static_cast<int>(sizeof(kGenesisLocations) / sizeof(kGenesisLocations[0]));

}  // namespace

NavigationFeature_B::NavigationFeature_B(EventBusInterface& eventBus)
    : Feature("NavigationFeature_B", eventBus)
    , Car(eventBus) {}

void NavigationFeature_B::execute() {
    const char line = getAbVariant().value_or('B');
    log(LogLevel::info,
        std::string("Car platform — NavigationFeature_B (B-line / Flag A|B=") + line + ")");
    runNavigationMainLoop();
}

void NavigationFeature_B::runNavigationMainLoop() {
    const int broadcastCount = 20;
    for (int i = 0; i < broadcastCount && !isStopRequested(); i++) {
        pollEvents();

        const int r = i % kGenesisRoutePointCount;

        log(LogLevel::info, "=== " + std::to_string(i + 1) + "회차 (B-line 내비) ===");

        log(LogLevel::info, std::string("차량 위치 전송: ") + kGenesisLocations[r]);
        if (!sendDomainEvent(EventId::VehicleLocationUpdate, kGenesisLocations[r])) {
            log(LogLevel::warn, "차량 위치 이벤트 전송 실패");
        }

        log(LogLevel::info, std::string("도로 정보 전송: ") + kGenesisRoadInfos[r]);
        if (!sendDomainEvent(EventId::RoadInfoUpdate, kGenesisRoadInfos[r])) {
            log(LogLevel::warn, "도로 정보 이벤트 전송 실패");
        }

        if (!sendDomainEvent(EventId::NavigationGuidanceUpdate,
                "NAV_GUIDANCE line=B step=" + std::to_string(i + 1) +
                " status=guiding")) {
            log(LogLevel::warn, "내비게이션 안내 이벤트 전송 실패");
        }

        if (i < broadcastCount - 1) {
            for (int s = 0; s < 5 && !isStopRequested(); ++s) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                pollEvents();
            }
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    log(LogLevel::info, "B-line 운행 종료. (QuitTheSystem 발행하지 않음)");
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
    sdv::feature::NavigationFeature_B feature(proxy);
    sdv::platform::lifecyclemgr::Feature::applyLaunchAbVariantFromArgv(argc, argv, feature.getFlag());
    return feature.run();
}
