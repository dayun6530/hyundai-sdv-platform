// Canonical Navigation feature (platform build). Docker/OTA demo: sdv-ota-manager/demo_src + build_standalone_demo.sh
#include "Feature/NavigationFeature.h"
#include "Feature/SubFeatures/SubFeatureModule.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <thread>
#include <chrono>
#include <memory>
#include <signal.h>
#include <string>
#include <utility>
#include <vector>
#include <functional>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace {

constexpr const char* kDistributionVersion = "1.0";

}  // namespace

namespace sdv::feature {

namespace {

using sdv::feature::sub::CommonCallbacks;
using sdv::feature::sub::ISubFeatureModule;

class NavLocationModule final : public ISubFeatureModule {
public:
    explicit NavLocationModule(CommonCallbacks cb, std::shared_ptr<std::string> payload)
        : cb_(std::move(cb)), payload_(std::move(payload)) {}
    const char* key() const override { return "LOCATION"; }
    void onTick() override {
        const std::string m = payload_ ? *payload_ : std::string();
        cb_.logInfo(std::string("차량 위치 전송: ") + m);
        if (!cb_.sendDomainEvent(static_cast<int>(EventId::VehicleLocationUpdate), m)) {
            cb_.logWarn("차량 위치 이벤트 전송 실패");
        }
    }

private:
    CommonCallbacks cb_;
    std::shared_ptr<std::string> payload_;
};

class NavRoadModule final : public ISubFeatureModule {
public:
    explicit NavRoadModule(CommonCallbacks cb, std::shared_ptr<std::string> payload)
        : cb_(std::move(cb)), payload_(std::move(payload)) {}
    const char* key() const override { return "ROAD"; }
    void onTick() override {
        const std::string m = payload_ ? *payload_ : std::string();
        cb_.logInfo(std::string("도로 정보 전송: ") + m);
        if (!cb_.sendDomainEvent(static_cast<int>(EventId::RoadInfoUpdate), m)) {
            cb_.logWarn("도로 정보 이벤트 전송 실패");
        }
    }

private:
    CommonCallbacks cb_;
    std::shared_ptr<std::string> payload_;
};

class NavGuidanceModule final : public ISubFeatureModule {
public:
    explicit NavGuidanceModule(CommonCallbacks cb, std::shared_ptr<std::string> payload)
        : cb_(std::move(cb)), payload_(std::move(payload)) {}
    const char* key() const override { return "GUIDANCE"; }
    void onTick() override {
        const std::string m = payload_ ? *payload_ : std::string();
        if (!cb_.sendDomainEvent(static_cast<int>(EventId::NavigationGuidanceUpdate), m)) {
            cb_.logWarn("내비게이션 안내 이벤트 전송 실패");
        }
    }

private:
    CommonCallbacks cb_;
    std::shared_ptr<std::string> payload_;
};

static std::vector<std::unique_ptr<ISubFeatureModule>> buildNavModules(
    CommonCallbacks cb,
    std::shared_ptr<std::string> loc,
    std::shared_ptr<std::string> road,
    std::shared_ptr<std::string> guidance) {
    std::vector<std::unique_ptr<ISubFeatureModule>> out;
    out.emplace_back(std::make_unique<NavLocationModule>(cb, std::move(loc)));
    out.emplace_back(std::make_unique<NavRoadModule>(cb, std::move(road)));
    out.emplace_back(std::make_unique<NavGuidanceModule>(cb, std::move(guidance)));
    return out;
}

}  // namespace

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
    : Feature("NavigationFeature", eventBus)
    , locPayload_(std::make_shared<std::string>())
    , roadPayload_(std::make_shared<std::string>())
    , guidancePayload_(std::make_shared<std::string>()) {
    CommonCallbacks cb {
        /*logInfo*/ [this](const std::string& m) { log(LogLevel::info, m); },
        /*logWarn*/ [this](const std::string& m) { log(LogLevel::warn, m); },
        /*sendDomainEvent*/ [this](int id, const std::string& msg) {
            return sendDomainEvent(static_cast<EventId>(id), msg);
        },
    };

    subTree_ = std::make_unique<sdv::feature::sub::CompositeNode>("NavigationRoot", "1.0");
    auto data = std::make_unique<sdv::feature::sub::CompositeNode>("NavData", "1.0");
    auto ux = std::make_unique<sdv::feature::sub::CompositeNode>("NavUx", "1.0");

    auto mods = buildNavModules(cb, locPayload_, roadPayload_, guidancePayload_);
    data->Add(std::make_unique<sdv::feature::sub::LeafNode>(std::move(mods[0]), "1.0"));
    data->Add(std::make_unique<sdv::feature::sub::LeafNode>(std::move(mods[1]), "1.0"));
    ux->Add(std::make_unique<sdv::feature::sub::LeafNode>(std::move(mods[2]), "1.0"));

    subTree_->Add(std::move(data));
    subTree_->Add(std::move(ux));
    subTree_->Load();
    subTree_->Start();
}

std::string NavigationFeature::distributionVersionLabelForLog() const {
    return kDistributionVersion;
}

void NavigationFeature::execute() {
    log(LogLevel::info, std::string("Navigation v") + kDistributionVersion + " — 연속 데모 브로드캐스트 (FLAG_OFF 시 일시 중단)");

    while (!isStopRequested()) {
        pollEvents();

        if (!flag_.isEnabled()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        log(LogLevel::info, "FLAG ON — 브로드캐스트 시작");
        while (flag_.isEnabled() && !isStopRequested()) {
            runNavigationMainLoop();
            if (!flag_.isEnabled() || isStopRequested()) {
                break;
            }
            log(LogLevel::info, "내비 라운드 완료 — 연속 데모 재개");
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

        if (locPayload_) *locPayload_ = navDemoLocation(r);
        if (roadPayload_) *roadPayload_ = navDemoRoad(r);
        if (guidancePayload_) *guidancePayload_ = navGuidanceForIteration(i + 1);

        const auto enabled = [this](const char* k) { return flag_.isSubFeatureEnabled(k); };
        if (subTree_) {
            subTree_->OnTick(enabled);
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
