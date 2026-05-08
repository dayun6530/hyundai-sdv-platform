#include "Feature/car/B/NavigationFeature_B.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"

#include <signal.h>
#include <string>

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
    const std::string line = getAbVariant().value_or("B");
    log(LogLevel::info,
        std::string("Car platform — NavigationFeature_B (B-line / Variant=") + line + ")");
    log(LogLevel::info, "[NAV-B] current location updated");
    log(LogLevel::info, "[NAV-B] route guidance started");
    log(LogLevel::info, "[NAV-B] real-time traffic rerouting enabled");
    NavigationFeature::execute();
}

int NavigationFeature_B::navDemoPointCount() {
    return kGenesisRoutePointCount;
}

const char* NavigationFeature_B::navDemoLocation(int rMod) {
    return kGenesisLocations[rMod % kGenesisRoutePointCount];
}

const char* NavigationFeature_B::navDemoRoad(int rMod) {
    return kGenesisRoadInfos[rMod % kGenesisRoutePointCount];
}

void NavigationFeature_B::navLogIterationBanner(int iteration1Based) {
    log(LogLevel::info, "=== " + std::to_string(iteration1Based) + "회차 (B-line 내비) ===");
}

std::string NavigationFeature_B::navGuidanceForIteration(int iteration1Based) {
    return "[NAV-B] NAV_GUIDANCE line=B step=" + std::to_string(iteration1Based) +
        " status=guiding traffic=live";
}

void NavigationFeature_B::navLogRunComplete() {
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
