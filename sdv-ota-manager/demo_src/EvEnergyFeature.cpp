// Standalone OTA 데모용: 단일 파일 g++ 빌드 (플랫폼 헤더/라이브러리 의존성 없음)
// 버전: argv[1] > DEMO_VERSION > kDefaultVersion

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "EvEnergyFeature";
constexpr const char* kDefaultVersion = "1.0";

std::string resolveVersion(int argc, char** argv) {
    if (argc >= 2 && argv && argv[1] && argv[1][0] != '\0') {
        return std::string(argv[1]);
    }
    if (const char* env = std::getenv("DEMO_VERSION")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    return std::string(kDefaultVersion);
}

const char* deployRoleTag() {
    const char* r = std::getenv("DEPLOY_ROLE");
    if (!r || r[0] == '\0') {
        return "unset";
    }
    if (std::strcmp(r, "shadow") == 0) {
        return "shadow";
    }
    if (std::strcmp(r, "primary") == 0) {
        return "primary";
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string version = resolveVersion(argc, argv);
    const char* role = deployRoleTag();

    std::cout << "[SYSTEM] " << kFeatureName << " DEPLOY_ROLE=" << role << " (Version: " << version
              << ") deployed.\n";
    std::cout << "[SYSTEM] EV energy daemon entering telemetry loop.\n";
    std::cout.flush();

    int tick = 0;
    int battery = 85;
    while (true) {
        battery -= 1;
        if (battery < 5) battery = 85;  // 데모용: 반복

        if (std::strcmp(role, "shadow") == 0) {
            std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                      << ") running [SHADOW observation]\n";
            std::cout << "[EV] tick=" << (++tick) << " | battery=" << battery
                      << "% | regen=shadow | range_est=" << (battery * 4) << "km\n";
        } else {
            std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                      << ") running [PRIMARY production]\n";
            std::cout << "[EV] tick=" << (++tick) << " | battery=" << battery
                      << "% | regen=live | range_est=" << (battery * 4) << "km\n";
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// 아래는 과거 플랫폼 연동 코드(헤더/라이브러리 의존) 잔재라 standalone 데모 빌드에서는 비활성화.
#if 0

// Standalone OTA 데모용: 단일 파일 g++ 빌드 (플랫폼 헤더/라이브러리 의존성 없음)
// 버전: argv[1] > DEMO_VERSION > kDefaultVersion

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "EvEnergyFeature";
constexpr const char* kDefaultVersion = "2.0";

std::string resolveVersion(int argc, char** argv) {
    if (argc >= 2 && argv && argv[1] && argv[1][0] != '\0') {
        return std::string(argv[1]);
    }
    if (const char* env = std::getenv("DEMO_VERSION")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    return std::string(kDefaultVersion);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string version = resolveVersion(argc, argv);

    std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version << ") is deployed!\n";
    std::cout << "[SYSTEM] EV energy daemon entering telemetry loop.\n";
    std::cout.flush();

    int tick = 0;
    int battery = 85;
    while (true) {
        battery -= 1;
        if (battery < 5) battery = 85;  // 데모용: 반복

        std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                  << ") is running! [Shadow Mode: Active]\n";
        std::cout << "[EV] tick=" << (++tick) << " | battery=" << battery
                  << "% | regen=shadow | range_est=" << (battery * 4) << "km\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// Standalone OTA 데모용: 플랫폼 내부 헤더(gRPC/EventBus/CMake) 없이 단일 g++로 빌드 가능.
// 버전: argv[1] > DEMO_VERSION 환경변수 > kDefaultVersion

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "EvEnergyFeature";
constexpr const char* kDefaultVersion = "2.0";

std::string resolveVersion(int argc, char** argv) {
    if (argc >= 2 && argv && argv[1] && argv[1][0] != '\0') {
        return std::string(argv[1]);
    }
    if (const char* env = std::getenv("DEMO_VERSION")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    return std::string(kDefaultVersion);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string version = resolveVersion(argc, argv);

    std::cout << "[SYSTEM] " << kFeatureName << " is deployed! (Version: " << version << ")\n";
    std::cout << "[SYSTEM] EV energy daemon entering telemetry loop.\n";
    std::cout.flush();

    int tick = 0;
    int battery = 85;
    while (true) {
        battery -= 1;
        if (battery < 5) battery = 85;  // 데모용: 반복

        std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                  << ") is running! [Shadow Mode: Active]\n";
        std::cout << "[EV] tick=" << (++tick) << " | battery=" << battery
                  << "% | regen=shadow | range_est=" << (battery * 4) << "km\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void EvEnergyFeature::execute() {
    // 💡 시작 로그에 버전 추가
    log(LogLevel::info, "EV 에너지 모니터링 대기 중 (Version: " + DEMO_VERSION + ") — FLAG_ON 수신 시 활성화");

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
                log(LogLevel::info, "최종 배터리 잔량: " + std::to_string(battery_) + "%");
                return;

            default:
                break;
            }
        }
    }
}

void EvEnergyFeature::runEvEnergyMainLoop() {
    // 💡 루프 시작 시 버전 추가
    log(LogLevel::info, "초기 배터리 잔량 (v" + DEMO_VERSION + "): " + std::to_string(battery_) + "%");
    log(LogLevel::info, "에너지 모니터링 대기 중...");

    while (!isStopRequested()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

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
                break;

            default:
                break;
            }
        }
    }
}

void EvEnergyFeature::processVehicleLocation(const std::string& message) {
    // 💡 위치 수신 로그에 버전 추가
    log(LogLevel::info, "[v" + DEMO_VERSION + "] 차량 위치 수신: " + message);
    battery_ -= 3;
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
    // 💡 도로 정보 수신 로그에 버전 추가
    log(LogLevel::info, "[v" + DEMO_VERSION + "] 도로 정보 수신: " + message);
    if (message.find("오르막") != std::string::npos) {
        if (flag_.isSubFeatureEnabled("ECO")) {
            log(LogLevel::warn, "오르막 감지: 에너지 소모 증가 예상, 에코 모드 권장");
        } else {
            log(LogLevel::info, "오르막 감지 (ECO OFF — 일반 경고만)");
        }
    } else if (message.find("내리막") != std::string::npos) {
        if (flag_.isSubFeatureEnabled("REGEN")) {
            log(LogLevel::info, "내리막 감지: 회생제동 최적화 모드 활성화");
            battery_ += 1;
            log(LogLevel::info, "회생제동으로 배터리 회수: " + std::to_string(battery_) + "%");
        } else {
            log(LogLevel::info, "REGEN: OFF — 회생제동 보정 생략");
        }
    } else {
        log(LogLevel::info, "평지 주행: 표준 에너지 모드 유지");
    }
    sendDomainEvent(EventId::EnergyStateUpdate,
        std::string("ENERGY_STATE roadHint=processed battery=") + std::to_string(battery_) +
        "|SHADOW_ROAD|" + message);
}

}  // namespace sdv::feature

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

#endif  // 0