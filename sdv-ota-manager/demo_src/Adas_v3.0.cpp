// Standalone OTA 데모용: gRPC/CMake 없이 g++ 단일 파일로 빌드 가능.
// 버전: argv[1] 이 있으면 사용, 없으면 아래 기본값.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "AdasFeature";
// 기본 배포 버전 (인자 없이 실행할 때). OTA 데모 시 이 한 줄만 바꿔도 됨.
constexpr const char* kDefaultVersion = "3.0";

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
              << ") — standalone g++ 빌드." << std::endl;
    if (std::strcmp(role, "shadow") == 0) {
        std::cout << "[SYSTEM] Shadow 파이프라인 관측 모드; 주기 텔레메트리 루프." << std::endl;
    } else if (std::strcmp(role, "primary") == 0) {
        std::cout << "[SYSTEM] Primary(운영) 경로; 주기 텔레메트리 루프." << std::endl;
    } else {
        std::cout << "[SYSTEM] DEPLOY_ROLE 미설정 — 로컬 단독 실행으로 간주." << std::endl;
    }
    std::cout.flush();

    while (true) {
        if (std::strcmp(role, "shadow") == 0) {
            std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                      << ") running [SHADOW observation]" << std::endl;
            std::cout << "[ADAS] fused tracks stable | planner shadow tick OK | no intervention request"
                      << std::endl;
        } else {
            std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                      << ") running [PRIMARY production]" << std::endl;
            std::cout << "[ADAS] fused tracks stable | planner live tick OK | intervention path armed"
                      << std::endl;
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
