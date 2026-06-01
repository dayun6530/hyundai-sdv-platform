// Canonical EV energy feature (platform build). Docker/OTA demo: sdv-ota-manager/demo_src + build_standalone_demo.sh
#include "Feature/EvEnergyFeature.h"
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

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace {

constexpr const char* kDistributionVersion = "1.0";

}  // namespace

namespace sdv::feature {

namespace {

using sdv::feature::sub::CommonCallbacks;
using sdv::feature::sub::ISubFeatureModule;

constexpr auto kRoadEventCooldown = std::chrono::seconds(45);
constexpr auto kDuplicateRoadSuppress = std::chrono::seconds(10);

int clampBatteryPercent(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

class EnergyRangeModule final : public ISubFeatureModule {
public:
    explicit EnergyRangeModule(CommonCallbacks cb, std::function<std::string()> msg)
        : cb_(std::move(cb)), msg_(std::move(msg)) {}
    const char* key() const override { return "RANGE"; }
    void onVehicleLocation(const std::string&) override {
        if (msg_) cb_.logInfo(msg_());
    }

private:
    CommonCallbacks cb_;
    std::function<std::string()> msg_;
};

class EnergyEcoModule final : public ISubFeatureModule {
public:
    explicit EnergyEcoModule(CommonCallbacks cb) : cb_(std::move(cb)) {}
    const char* key() const override { return "ECO"; }
    void onVehicleLocation(const std::string&) override {
        cb_.logInfo("ECO: 효율 주행 힌트 적용");
    }
    void onRoadInfo(const std::string& road) override {
        if (road.find("오르막") != std::string::npos) {
            cb_.logWarn("오르막 감지: 에너지 소모 증가 예상, 에코 모드 권장");
        } else {
            cb_.logInfo("ECO: 도로 기반 효율 주행 힌트 반영");
        }
    }

private:
    CommonCallbacks cb_;
};

class EnergyRegenModule final : public ISubFeatureModule {
public:
    explicit EnergyRegenModule(CommonCallbacks cb,
        std::function<void()> regenOnDownhill)
        : cb_(std::move(cb)), regenOnDownhill_(std::move(regenOnDownhill)) {}
    const char* key() const override { return "REGEN"; }
    void onVehicleLocation(const std::string&) override {
        cb_.logInfo("REGEN: 위치 기반 회생 힌트 활성");
    }
    void onRoadInfo(const std::string& road) override {
        if (road.find("내리막") != std::string::npos) {
            cb_.logInfo("내리막 감지: 회생제동 최적화 모드 활성화");
            if (regenOnDownhill_) regenOnDownhill_();
        } else {
            cb_.logInfo("REGEN: 도로 기반 회생 보정 점검");
        }
    }

private:
    CommonCallbacks cb_;
    std::function<void()> regenOnDownhill_;
};

static std::vector<std::unique_ptr<ISubFeatureModule>> buildEnergyModules(
    CommonCallbacks cb,
    std::function<std::string()> rangeLogLine,
    std::function<void()> regenOnDownhill) {
    std::vector<std::unique_ptr<ISubFeatureModule>> out;
    out.emplace_back(std::make_unique<EnergyRangeModule>(cb, std::move(rangeLogLine)));
    out.emplace_back(std::make_unique<EnergyEcoModule>(cb));
    out.emplace_back(std::make_unique<EnergyRegenModule>(cb, std::move(regenOnDownhill)));
    return out;
}

}  // namespace

EvEnergyFeature::EvEnergyFeature(EventBusInterface& eventBus)
    : Feature("EvEnergyFeature", eventBus), battery_(100) {
    CommonCallbacks cb {
        /*logInfo*/ [this](const std::string& m) { log(LogLevel::info, m); },
        /*logWarn*/ [this](const std::string& m) { log(LogLevel::warn, m); },
        /*sendDomainEvent*/ [this](int id, const std::string& msg) {
            return sendDomainEvent(static_cast<EventId>(id), msg);
        },
    };
    subTree_ = std::make_unique<sdv::feature::sub::CompositeNode>("EnergyRoot", "1.0");
    auto core = std::make_unique<sdv::feature::sub::CompositeNode>("EnergyCore", "1.0");
    auto mods = buildEnergyModules(
        cb,
        /*rangeLogLine*/ [this]() {
            return "RANGE: 예상 주행거리 산출 — 배터리 " + std::to_string(battery_) +
                "% | " + std::to_string(battery_ * 4) + "km";
        },
        /*regenOnDownhill*/ []() {});
    for (auto& m : mods) {
        core->Add(std::make_unique<sdv::feature::sub::LeafNode>(std::move(m), "1.0"));
    }
    subTree_->Add(std::move(core));
    subTree_->Load();
    subTree_->Start();
}

std::string EvEnergyFeature::distributionVersionLabelForLog() const {
    return kDistributionVersion;
}

void EvEnergyFeature::execute() {
    const std::string v = !effectiveVersionLabelForLog().empty()
        ? effectiveVersionLabelForLog()
        : std::string(kDistributionVersion);
    log(LogLevel::info, std::string("EV Energy v") + v + " — FLAG_ON 수신 시 활성화");
    sendTelemetry("INFO", "EV_STARTUP",
        std::string("{\"version\":\"") + v + "\",\"battery_init\":" + std::to_string(battery_) + "}");

    auto lastBatteryDrainAt = std::chrono::steady_clock::now();
    constexpr auto kBatteryDrainInterval = std::chrono::minutes(1);

    while (!isStopRequested()) {
        sleepTelemetryPublishInterval();

        auto events = pollEvents();

        if (!flag_.isEnabled()) {
            // Feature OFF during the loop: pause drain timing to avoid "catch-up" drops on re-enable.
            lastBatteryDrainAt = std::chrono::steady_clock::now();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        while (now - lastBatteryDrainAt >= kBatteryDrainInterval) {
            lastBatteryDrainAt += kBatteryDrainInterval;
            if (battery_ > 0) {
                battery_ = clampBatteryPercent(battery_ - 1);
                log(LogLevel::info, "배터리 자연 소모: " + std::to_string(battery_) + "% (분당 1%)");
                sendTelemetry("INFO", "EV_BATTERY_DRAIN",
                    std::string("{\"battery\":") + std::to_string(battery_) + ",\"drain_per_minute\":1}");
            }
            if (checkBatteryEmptyAndQuit()) {
                return;
            }
        }

        for (const auto& event : events) {
            switch (event.getEventId()) {
            case EventId::VehicleLocationUpdate:
                processVehicleLocation(event.getMessage());
                break;

            case EventId::RoadInfoUpdate:
                processRoadInfo(event.getMessage());
                if (checkBatteryEmptyAndQuit()) {
                    return;
                }
                break;

            case EventId::QuitTheSystem:
                log(LogLevel::info, "종료 신호 수신");
                log(LogLevel::info, "최종 배터리 잔량: " + std::to_string(battery_) + "%");
                sendTelemetry("INFO", "SYSTEM_QUIT",
                    std::string("{\"reason\":\"QuitTheSystem\",\"battery_final\":") + std::to_string(battery_) + "}");
                return;

            default:
                break;
            }
        }
    }
}

void EvEnergyFeature::runEvEnergyMainLoop() {
    log(LogLevel::info, "초기 배터리 잔량: " + std::to_string(battery_) + "%");
    log(LogLevel::info, "에너지 모니터링 대기 중...");
    sendTelemetry("INFO", "EV_MONITOR_READY",
        std::string("{\"battery\":") + std::to_string(battery_) + "}");

    while (!isStopRequested()) {
        sleepTelemetryPublishInterval();

        auto events = pollEvents();
        for (const auto& event : events) {
            if (event.getEventId() == EventId::QuitTheSystem) {
                log(LogLevel::info, "종료 신호 수신");
                log(LogLevel::info, "최종 배터리 잔량: " + std::to_string(battery_) + "%");
                return;
            }
            if (!flag_.isEnabled()) {
                continue;
            }
            switch (event.getEventId()) {
            case EventId::VehicleLocationUpdate:
                processVehicleLocation(event.getMessage());
                break;

            case EventId::RoadInfoUpdate:
                processRoadInfo(event.getMessage());
                if (checkBatteryEmptyAndQuit()) {
                    return;
                }
                break;

            default:
                break;
            }
        }
    }
}

bool EvEnergyFeature::checkBatteryEmptyAndQuit() {
    if (battery_ > 0) {
        return false;
    }
    battery_ = 0;
    log(LogLevel::warn, "배터리 0% 도달 — 차량 시스템 종료 이벤트 전송");
    sendTelemetry("WARN", "EV_BATTERY_EMPTY", "{\"battery\":0,\"action\":\"QuitTheSystem\"}");
    sendDomainEvent(EventId::QuitTheSystem, "EV_BATTERY_EMPTY");
    return true;
}

void EvEnergyFeature::applyRoadBatteryModifiers(const std::string& message) {
    const auto now = std::chrono::steady_clock::now();

    if (message.find("오르막") != std::string::npos) {
        if (!flag_.isSubFeatureEnabled("ECO")) {
            log(LogLevel::info, "오르막 감지 (ECO OFF — SOC 보정 생략)");
            return;
        }
        if ((now - lastUphillDrainAt_) < kRoadEventCooldown) {
            log(LogLevel::info, "오르막 SOC 보정 쿨다운 중 — 생략");
            return;
        }
        lastUphillDrainAt_ = now;
        if (battery_ > 0) {
            battery_ = clampBatteryPercent(battery_ - 1);
            log(LogLevel::warn,
                "오르막 추가 소모: " + std::to_string(battery_) + "% (ECO 활성, 45초 쿨다운)");
            sendTelemetry("WARN", "EV_UPHILL_DRAIN",
                std::string("{\"road_info\":\"") + message + "\",\"battery\":" + std::to_string(battery_) +
                ",\"delta\":-1}");
        }
        return;
    }

    if (message.find("내리막") != std::string::npos) {
        if (!flag_.isSubFeatureEnabled("REGEN")) {
            log(LogLevel::info, "REGEN: OFF — 회생 SOC 보정 생략");
            return;
        }
        if (battery_ >= 100) {
            log(LogLevel::info, "배터리 만충 — 회생 보정 생략");
            return;
        }
        if ((now - lastDownhillRegenAt_) < kRoadEventCooldown) {
            log(LogLevel::info, "회생 SOC 보정 쿨다운 중 — 생략");
            return;
        }
        lastDownhillRegenAt_ = now;
        battery_ = clampBatteryPercent(battery_ + 1);
        log(LogLevel::info, "회생제동으로 배터리 회수: " + std::to_string(battery_) + "% (45초 쿨다운)");
        sendTelemetry("INFO", "EV_REGEN_APPLIED",
            std::string("{\"road_info\":\"") + message + "\",\"battery\":" + std::to_string(battery_) +
            ",\"delta\":1}");
    }
}

void EvEnergyFeature::processVehicleLocation(const std::string& message) {
    log(LogLevel::info, "차량 위치 수신: " + message);

    // 팀원 브랜치 반영: 위치 수신 Telemetry 전송
    sendTelemetry("INFO", "EV_LOCATION_RX",
        std::string("{\"location\":\"") + message + "\",\"battery\":" + std::to_string(battery_) + "}");

    // 내 브랜치 반영: 리팩토링된 subTree_ 호출 구조 유지
    const auto enabled = [this](const char* k) { return flag_.isSubFeatureEnabled(k); };
    if (subTree_) {
        subTree_->OnVehicleLocation(enabled, message);
    }

    // 팀원 브랜치 반영: 서브 기능별 로그 처리
    if (flag_.isSubFeatureEnabled("RANGE")) {
        log(LogLevel::info, "RANGE: 예상 주행거리 산출 — 배터리 " + std::to_string(battery_) +
            "% | " + std::to_string(battery_ * 4) + "km");
    } else {
        log(LogLevel::info, "RANGE: OFF — 주행거리 추정 생략");
    }
    if (flag_.isSubFeatureEnabled("ECO")) {
        log(LogLevel::info, "ECO: 효율 주행 힌트 적용");
    } else {
        log(LogLevel::info, "ECO: OFF — 에코 힌트 생략");
    }
    if (flag_.isSubFeatureEnabled("REGEN")) {
        log(LogLevel::info, "REGEN: 위치 기반 회생 힌트 활성");
    } else {
        log(LogLevel::info, "REGEN: OFF — 회생 힌트 생략");
    }

    sendDomainEvent(EventId::EnergyStateUpdate,
        std::string("ENERGY_STATE battery=") + std::to_string(battery_) +
        " rangeKm=" + std::to_string(battery_ * 4) + "|SHADOW_LOC|" + message);
}

void EvEnergyFeature::processRoadInfo(const std::string& message) {
    const auto now = std::chrono::steady_clock::now();
    if (!lastRoadMessage_.empty() && message == lastRoadMessage_ &&
        (now - lastRoadMessageAt_) < kDuplicateRoadSuppress) {
        return;
    }
    lastRoadMessage_ = message;
    lastRoadMessageAt_ = now;

    log(LogLevel::info, "도로 정보 수신: " + message);

    const bool anyHill = (message.find("오르막") != std::string::npos) ||
        (message.find("내리막") != std::string::npos);
    if (!anyHill) {
        log(LogLevel::info, "평지 주행: 표준 에너지 모드 유지");
    }

    applyRoadBatteryModifiers(message);

    // 내 브랜치 반영: subTree_ 처리
    const auto enabled = [this](const char* k) { return flag_.isSubFeatureEnabled(k); };
    if (subTree_) {
        subTree_->OnRoadInfo(enabled, message);
    }
    
    sendDomainEvent(EventId::EnergyStateUpdate,
        std::string("ENERGY_STATE roadHint=processed battery=") + std::to_string(battery_) +
        "|SHADOW_ROAD|" + message);
}

}
#ifndef SDV_FEATURE_NO_MAIN
int main(int argc, char* argv[]) {
    std::string address = "localhost:50051";
    if (argc > 1) address = argv[1];

    signal(SIGTERM, sdv::platform::lifecyclemgr::Feature::signalHandler);
    signal(SIGINT, sdv::platform::lifecyclemgr::Feature::signalHandler);

    sdv::platform::communications::eventbus::EventBusProxy proxy(address);
    sdv::feature::EvEnergyFeature feature(proxy);
    return feature.run();
}
#endif
