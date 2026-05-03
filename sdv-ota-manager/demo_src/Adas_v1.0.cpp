// Standalone OTA demo (single-file g++ in Docker). Source line: v1.0
// Canonical platform: sibling sdv-feature-flag-platform/src/Feature/src/AdasFeature.cpp
// Version: argv[1] if set, else DEMO_VERSION env, else kDefaultVersion.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "AdasFeature";
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
