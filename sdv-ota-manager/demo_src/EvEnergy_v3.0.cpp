// Standalone OTA demo (EvEnergy) — v3.0 candidate/promote logs.
// Built by: scripts/build_standalone_demo.sh --domain EvEnergy --version 3.0

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "EvEnergyFeature";
constexpr const char* kDefaultVersion = "3.0";

std::string resolveVersion(int argc, char** argv) {
    if (argc >= 2 && argv && argv[1] && argv[1][0] != '\0') {
        return std::string(argv[1]);
    }
    if (const char* env = std::getenv("DEMO_VERSION")) {
        if (env[0] != '\0') return std::string(env);
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
              << ") — v3.0 demo build." << std::endl;

    if (std::strcmp(role, "shadow") == 0) {
        std::cout << "[EV v3.0] Candidate Shadow version is running" << std::endl;
        std::cout << "[EV v3.0] Shadow energy telemetry + range model enabled" << std::endl;
    } else if (std::strcmp(role, "primary") == 0) {
        std::cout << "[EV v3.0] Primary production path running" << std::endl;
        std::cout << "[EV v3.0] Live battery + charging optimization enabled (v3.0)" << std::endl;
    } else {
        std::cout << "[EV v3.0] Local run (DEPLOY_ROLE unset)" << std::endl;
    }
    std::cout.flush();

    int tick = 0;
    int battery = 85;
    while (true) {
        battery -= 1;
        if (battery < 5) battery = 85;
        ++tick;

        if (std::strcmp(role, "shadow") == 0) {
            std::cout << "[EV v3.0] shadow tick OK | compare-only | battery=" << battery
                      << "% | range_est=" << (battery * 4) << "km" << std::endl;
        } else {
            std::cout << "[EV v3.0] primary tick OK | battery=" << battery << "% | range_est="
                      << (battery * 4) << "km" << std::endl;
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
