// Standalone OTA demo (ADAS) — distinct v1.0 logs.
// Built by: scripts/build_standalone_demo.sh --domain ADAS --version 1.0

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
    if (!r || r[0] == '\0') return "unset";
    if (std::strcmp(r, "shadow") == 0) return "shadow";
    if (std::strcmp(r, "primary") == 0) return "primary";
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string version = resolveVersion(argc, argv);
    const char* role = deployRoleTag();

    std::cout << "[SYSTEM] " << kFeatureName << " DEPLOY_ROLE=" << role << " (Version: " << version
              << ") — v1.0 demo build." << std::endl;

    if (std::strcmp(role, "shadow") == 0) {
        std::cout << "[ADAS v1.0] Candidate Shadow version is running" << std::endl;
        std::cout << "[ADAS v1.0] Next-gen safety gate + telemetry enrichment enabled" << std::endl;
        std::cout << "[ADAS v1.0] Shadow-only decision log generated" << std::endl;
    } else if (std::strcmp(role, "primary") == 0) {
        std::cout << "[ADAS v1.0] Primary production path running" << std::endl;
        std::cout << "[ADAS v1.0] Full-stack perception enabled (v1.0)" << std::endl;
    } else {
        std::cout << "[ADAS v1.0] Local run (DEPLOY_ROLE unset)" << std::endl;
    }
    std::cout.flush();

    while (true) {
        if (std::strcmp(role, "shadow") == 0) {
            std::cout << "[ADAS v1.0] shadow tick OK | compare-only | enhanced decision trace" << std::endl;
        } else {
            std::cout << "[ADAS v1.0] primary tick OK | actuation enabled" << std::endl;
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
