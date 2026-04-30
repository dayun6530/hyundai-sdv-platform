#include "Feature/AdasFeature.h"
#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include <signal.h>

using namespace sdv::platform::communications::eventbus;
using namespace sdv::platform::lifecyclemgr;

namespace sdv::feature {

AdasFeature::AdasFeature(EventBusInterface& eventBus)
    : Feature("AdasFeature", eventBus) {}

void AdasFeature::execute() {
    log(LogLevel::info, "ADAS 실행 — 메인 FLAG 및 서브(FCA/LKA/SCC)에 따라 처리");

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
    if (flag_.isSubFeatureEnabled("FCA")) {
        log(LogLevel::info, "FCA: 전방 객체 감지 및 충돌 위험 평가");
    } else {
        log(LogLevel::info, "FCA: OFF (전방충돌방지 비활성)");
    }
    if (flag_.isSubFeatureEnabled("LKA")) {
        log(LogLevel::info, "LKA: 차선 유지 보조 활성");
    } else {
        log(LogLevel::info, "LKA: OFF (차선유지 비활성)");
    }
    if (flag_.isSubFeatureEnabled("SCC")) {
        log(LogLevel::info, "SCC: 스마트 크루즈 제어 추종");
    } else {
        log(LogLevel::info, "SCC: OFF (어댑티브 크루즈 비활성)");
    }
    log(LogLevel::info, "주변 차량/장애물 감지 영역 업데이트");
    sendDomainEvent(EventId::AdasDecisionUpdate,
        std::string("ADAS_DECISION source=location status=tracking|") + message);
}

void AdasFeature::processRoadInfo(const std::string& message) {
    log(LogLevel::info, "도로 정보 수신: " + message);
    log(LogLevel::info, "자율주행 경로 및 속도 계획 반영");
    if (flag_.isSubFeatureEnabled("FCA")) {
        log(LogLevel::info, "FCA: 위험 상황 예측 및 감속 권고 평가");
    } else {
        log(LogLevel::info, "FCA: OFF — 전방 위험 평가 생략");
    }
    if (flag_.isSubFeatureEnabled("LKA")) {
        log(LogLevel::info, "LKA: 차선 유지 (도로 기준 궤적 보정)");
    } else {
        log(LogLevel::info, "LKA: OFF — 차선 궤적 보정 생략");
    }
    if (flag_.isSubFeatureEnabled("SCC")) {
        log(LogLevel::info, "SCC: 도로 속도 한도 반영 추종");
    } else {
        log(LogLevel::info, "SCC: OFF — 속도 추종 생략");
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
    //   버전 생략 시 아래 기본값 (v2.0 데모 시 소스에서 한 줄만 바꿔 재빌드하면 됨)
    static constexpr const char* kDefaultOtaDemoVersion = "2.0";

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
