// Canonical Navigation feature (platform build). Docker/OTA demo: sdv-ota-manager/demo_src + build_standalone_demo.sh
#include "Feature/NavigationFeature.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace {

constexpr const char* kDistributionVersion = "1.0";

}  // namespace

namespace sdv::feature {

static const char* locations[] = {
    "서울 강남구 테헤란로 (37.5012, 127.0396)",
    "서울 서초구 반포대로 (37.4923, 127.0068)",
    "경기 성남시 분당구 판교로 (37.3948, 127.1112)",
    "경기 용인시 수지구 죽전로 (37.3245, 127.1058)",
    "경기 화성시 동탄대로 (37.2064, 127.0718)"
};

static const char* roadInfos[] = {
    "직진 3km 도심도로 (제한속도 60km/h, 평지, 신호 5개)",
    "우회전 후 직진 5km 간선도로 (제한속도 80km/h, 완만한 내리막)",
    "고속도로 진입 8km (제한속도 110km/h, 평지, 터널 1개)",
    "고속도로 직진 10km (제한속도 100km/h, 오르막 경사 3%)",
    "고속도로 출구 → 시내도로 2km (제한속도 60km/h, 내리막 경사 5%)"
};

static constexpr int kRoutePointCount =
    static_cast<int>(sizeof(locations) / sizeof(locations[0]));

NavigationFeature::NavigationFeature(EventBusInterface& eventBus)
    : Feature("NavigationFeature", eventBus) {}

std::string NavigationFeature::distributionVersionLabelForLog() const {
    return kDistributionVersion;
}

void NavigationFeature::execute() {
    const std::string v = !effectiveVersionLabelForLog().empty()
        ? effectiveVersionLabelForLog()
        : std::string(kDistributionVersion);
    log(LogLevel::info,
        std::string("Navigation v") + v +
        " — 연속 데모 브로드캐스트 (FLAG_OFF 시 일시 중단)");
    sendTelemetry("INFO", "NAV_STARTUP",
        std::string("{\"version\":\"") + v + "\"}");

    while (!isStopRequested()) {
        pollEvents();

        if (!flag_.isEnabled()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        log(LogLevel::info, "FLAG ON — 브로드캐스트 시작");
        sendTelemetry("INFO", "NAV_FLAG_ON", "{\"flag\":\"enabled\"}");
        while (flag_.isEnabled() && !isStopRequested()) {
            runNavigationMainLoop();
            if (!flag_.isEnabled() || isStopRequested()) {
                break;
            }
            log(LogLevel::info, "내비 라운드 완료 — 연속 데모 재개");
            sendTelemetry("INFO", "NAV_ROUND_COMPLETE", "{\"status\":\"complete\"}");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void NavigationFeature::runNavigationMainLoop() {
    const int mod = navDemoPointCount();
    for (int i = 0; i < broadcastCount_ && !isStopRequested(); i++) {
        pollEvents();

        if (!flag_.isEnabled()) {
            // FLAG_OFF mid-run: stop emitting domain output immediately.
            return;
        }

        const int r = i % mod;

        navLogIterationBanner(i + 1);

        if (flag_.isSubFeatureEnabled("LOCATION")) {
            log(LogLevel::info, std::string("차량 위치 전송: ") + navDemoLocation(r));
            sendTelemetry("INFO", "NAV_LOCATION_TX",
                std::string("{\"location\":\"") + navDemoLocation(r) + "\"}");
            if (!sendDomainEvent(EventId::VehicleLocationUpdate, navDemoLocation(r))) {
                log(LogLevel::warn, "차량 위치 이벤트 전송 실패");
                sendTelemetry("WARN", "NAV_LOCATION_TX_FAIL",
                    std::string("{\"location\":\"") + navDemoLocation(r) + "\"}");
            }
        } else {
            log(LogLevel::info, "LOCATION: OFF — 위치 이벤트 생략");
        }

        if (flag_.isSubFeatureEnabled("ROAD")) {
            log(LogLevel::info, std::string("도로 정보 전송: ") + navDemoRoad(r));
            sendTelemetry("INFO", "NAV_ROAD_TX",
                std::string("{\"road_info\":\"") + navDemoRoad(r) + "\"}");
            if (!sendDomainEvent(EventId::RoadInfoUpdate, navDemoRoad(r))) {
                log(LogLevel::warn, "도로 정보 이벤트 전송 실패");
                sendTelemetry("WARN", "NAV_ROAD_TX_FAIL",
                    std::string("{\"road_info\":\"") + navDemoRoad(r) + "\"}");
            }
        } else {
            log(LogLevel::info, "ROAD: OFF — 도로 이벤트 생략");
        }

        if (flag_.isSubFeatureEnabled("GUIDANCE")) {
            const std::string guidance = navGuidanceForIteration(i + 1);
            if (!sendDomainEvent(EventId::NavigationGuidanceUpdate, guidance)) {
                log(LogLevel::warn, "내비게이션 안내 이벤트 전송 실패");
                sendTelemetry("WARN", "NAV_GUIDANCE_TX_FAIL",
                    std::string("{\"guidance\":\"") + guidance + "\"}");
            }
        } else {
            log(LogLevel::info, "GUIDANCE: OFF — 안내 이벤트 생략");
        }

        if (i < broadcastCount_ - 1) {
            for (int s = 0; s < 5 && !isStopRequested(); ++s) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                pollEvents();
                if (!flag_.isEnabled()) {
                    return;
                }
            }
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    // NOTE: Navigation does not publish QuitTheSystem (keeps control demos stable).
    navLogRunComplete();
}

int NavigationFeature::navDemoPointCount() {
    return kRoutePointCount;
}

const char* NavigationFeature::navDemoLocation(int rMod) {
    return locations[rMod % kRoutePointCount];
}

const char* NavigationFeature::navDemoRoad(int rMod) {
    return roadInfos[rMod % kRoutePointCount];
}

void NavigationFeature::navLogIterationBanner(int iteration1Based) {
    log(LogLevel::info, "=== " + std::to_string(iteration1Based) + "회차 정보 브로드캐스트 ===");
}

std::string NavigationFeature::navGuidanceForIteration(int iteration1Based) {
    return "NAV_GUIDANCE step=" + std::to_string(iteration1Based) + " status=guiding";
}

void NavigationFeature::navLogRunComplete() {
    log(LogLevel::info, "내비 라운드 종료 — 다음 라운드로 계속 (연속 데모)");
}

}  // namespace sdv::feature

#ifndef SDV_FEATURE_NO_MAIN
int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::NavigationFeature feature(proxy);
    return feature.run();
}
#endif
