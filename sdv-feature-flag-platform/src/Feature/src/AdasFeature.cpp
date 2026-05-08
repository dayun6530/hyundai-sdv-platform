// Canonical ADAS feature (platform build). Docker/OTA demo images: sdv-ota-manager/demo_src + build_standalone_demo.sh
#include "Feature/AdasFeature.h"
#include "Feature/SubFeatures/SubFeatureModule.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <signal.h>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace {

/** In-repo distribution line for logs/telemetry (not argv Docker tag). Bump with release. */
constexpr const char* kDistributionVersion = "1.0";

}  // namespace

namespace sdv::feature {

namespace {

using sdv::feature::sub::CommonCallbacks;
using sdv::feature::sub::ISubFeatureModule;

class AdasFcaModule final : public ISubFeatureModule {
public:
    explicit AdasFcaModule(CommonCallbacks cb) : cb_(std::move(cb)) {}
    const char* key() const override { return "FCA"; }
    void onVehicleLocation(const std::string&) override {
        cb_.logInfo("FCA: 전방 객체 감지 및 충돌 위험 평가");
    }
    void onRoadInfo(const std::string&) override {
        cb_.logInfo("FCA: 위험 상황 예측 및 감속 권고 평가");
    }

private:
    CommonCallbacks cb_;
};

class AdasLkaModule final : public ISubFeatureModule {
public:
    explicit AdasLkaModule(CommonCallbacks cb) : cb_(std::move(cb)) {}
    const char* key() const override { return "LKA"; }
    void onVehicleLocation(const std::string&) override {
        cb_.logInfo("LKA: 차선 유지 보조 활성");
    }
    void onRoadInfo(const std::string&) override {
        cb_.logInfo("LKA: 차선 유지 (도로 기준 궤적 보정)");
    }

private:
    CommonCallbacks cb_;
};

class AdasSccModule final : public ISubFeatureModule {
public:
    explicit AdasSccModule(CommonCallbacks cb) : cb_(std::move(cb)) {}
    const char* key() const override { return "SCC"; }
    void onVehicleLocation(const std::string&) override {
        cb_.logInfo("SCC: 스마트 크루즈 제어 추종");
    }
    void onRoadInfo(const std::string&) override {
        cb_.logInfo("SCC: 도로 속도 한도 반영 추종");
    }

private:
    CommonCallbacks cb_;
};

static std::vector<std::unique_ptr<ISubFeatureModule>> buildAdasModules(CommonCallbacks cb) {
    std::vector<std::unique_ptr<ISubFeatureModule>> out;
    out.emplace_back(std::make_unique<AdasFcaModule>(cb));
    out.emplace_back(std::make_unique<AdasLkaModule>(cb));
    out.emplace_back(std::make_unique<AdasSccModule>(cb));
    return out;
}

}  // namespace

AdasFeature::AdasFeature(EventBusInterface& eventBus)
    : Feature("AdasFeature", eventBus) {
    CommonCallbacks cb {
        /*logInfo*/ [this](const std::string& m) { log(LogLevel::info, m); },
        /*logWarn*/ [this](const std::string& m) { log(LogLevel::warn, m); },
        /*sendDomainEvent*/ [this](int id, const std::string& msg) {
            return sendDomainEvent(static_cast<EventId>(id), msg);
        },
    };
    subTree_ = std::make_unique<sdv::feature::sub::CompositeNode>("AdasRoot", "1.0");
    auto core = std::make_unique<sdv::feature::sub::CompositeNode>("AdasCore", "1.0");
    auto mods = buildAdasModules(cb);
    for (auto& m : mods) {
        core->Add(std::make_unique<sdv::feature::sub::LeafNode>(std::move(m), "1.0"));
    }
    subTree_->Add(std::move(core));
    subTree_->Load();
    subTree_->Start();
}

std::string AdasFeature::distributionVersionLabelForLog() const {
    return kDistributionVersion;
}

void AdasFeature::execute() {
    log(LogLevel::info, std::string("ADAS v") + kDistributionVersion + " — 메인 FLAG 및 서브(FCA/LKA/SCC)에 따라 처리");

    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto events = pollEvents();

        if (!flag_.isEnabled()) {
            continue;
        }

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

void AdasFeature::runAdasMainLoop() {
    log(LogLevel::info, "자율주행 대기 중...");

    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto events = pollEvents();
        // Match execute(): drain control events in pollEvents, then skip domain work while main FLAG OFF.
        if (!flag_.isEnabled()) {
            continue;
        }
        for (const auto& event : events) {
            if (event.getEventId() == EventId::QuitTheSystem) {
                log(LogLevel::info, "종료 신호 수신");
                return;
            }
            switch (event.getEventId()) {
            case EventId::VehicleLocationUpdate:
                processVehicleLocation(event.getMessage());
                break;

            case EventId::RoadInfoUpdate:
                processRoadInfo(event.getMessage());
                break;

            default:
                break;
            }
        }
    }
}

void AdasFeature::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "현재 위치 수신: " + message);
    const auto enabled = [this](const char* k) { return flag_.isSubFeatureEnabled(k); };
    if (subTree_) {
        subTree_->OnVehicleLocation(enabled, message);
    }
    log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");
    sendDomainEvent(EventId::AdasDecisionUpdate,
        std::string("ADAS_DECISION source=location status=tracking|") + message);
}

void AdasFeature::processRoadInfo(const std::string& message) {
    log(LogLevel::info, "도로 정보 수신: " + message);
    log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
    const auto enabled = [this](const char* k) { return flag_.isSubFeatureEnabled(k); };
    if (subTree_) {
        subTree_->OnRoadInfo(enabled, message);
    }
    log(LogLevel::info, "자율주행 중...");
    sendDomainEvent(EventId::AdasDecisionUpdate,
        std::string("ADAS_DECISION source=road status=planning|") + message);
}

}  // namespace sdv::feature

#ifndef SDV_FEATURE_NO_MAIN

namespace {

// OTA/도커 데모용: gRPC 없이 표준 출력으로 배포·주기 로그만 남김 (docker logs 검증용)
void runOtaDemoLoop(const char* versionLabel) {
    std::cout << "[SYSTEM] AdasFeature_v" << versionLabel << " is deployed!" << std::endl;
    std::cout.flush();
    while (true) {
        std::cout << "[SYSTEM] AdasFeature is running (Version: " << versionLabel << ")..." << std::endl;
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    // 사용법: AdasFeature --ota-demo [버전]
    //   버전 생략 시 아래 기본값 (플랫폼 기본 라인과 맞춤; 데모만 바꿀 때만 수정)
    static constexpr const char* kDefaultOtaDemoVersion = "1.0";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ota-demo") == 0) {
            const char* ver = kDefaultOtaDemoVersion;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ver = argv[++i];
            }
            runOtaDemoLoop(ver);
            return 0;
        }
    }

    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::AdasFeature feature(proxy);
    return feature.run();
}
#endif
