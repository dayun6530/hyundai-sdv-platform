#include "Feature/NavigationFeature.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <signal.h>
#include <string>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

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

void NavigationFeature::execute() {
    for (int i = 0; i < broadcastCount_ && !isStopRequested(); i++) {
        pollEvents();

        const int r = i % kRoutePointCount;

        log(LogLevel::info, "=== " + std::to_string(i + 1) + "회차 정보 브로드캐스트 ===");

        auto sendForVariants = [this](EventId id, const std::string& payloadBase,
                                      const char* ctxLabel) {
            bool any = false;
            if (isAbPathAEnabled()) {
                any = true;
                if (!sendDomainEvent(id, payloadBase + " variant=A")) {
                    log(LogLevel::warn, std::string(ctxLabel) + " 전송 실패 (A)");
                }
            }
            if (isAbPathBEnabled()) {
                any = true;
                if (!sendDomainEvent(id, payloadBase + " variant=B")) {
                    log(LogLevel::warn, std::string(ctxLabel) + " 전송 실패 (B)");
                }
            }
            if (!any) {
                log(LogLevel::warn,
                    "A/B 경로 모두 비활성, " + std::string(ctxLabel) + " 생략");
            }
        };

        log(LogLevel::info, std::string("차량 위치 전송: ") + locations[r]);
        sendForVariants(EventId::VehicleLocationUpdate, locations[r], "차량 위치");

        log(LogLevel::info, std::string("도로 정보 전송: ") + roadInfos[r]);
        sendForVariants(EventId::RoadInfoUpdate, roadInfos[r], "도로 정보");

        sendForVariants(
            EventId::NavigationGuidanceUpdate,
            "NAV_GUIDANCE step=" + std::to_string(i + 1) + " status=guiding",
            "내비게이션 안내");

        if (i < broadcastCount_ - 1) {
            for (int s = 0; s < 5 && !isStopRequested(); ++s) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                pollEvents();
            }
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    // NOTE: 데모/운영에서 단일 Feature가 전체 시스템 종료를 트리거하면
    // KillSwitch 등 제어 데모가 불안정해질 수 있으므로, NavigationFeature는
    // 운행 종료 시 QuitTheSystem을 발행하지 않는다.
    log(LogLevel::info, "운행 종료. (QuitTheSystem 발행하지 않음)");
}

}  // namespace sdv::feature

int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::NavigationFeature feature(proxy);
    return feature.run();
}
