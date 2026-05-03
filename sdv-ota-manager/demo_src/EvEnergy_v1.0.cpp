// Standalone OTA demo (single-file g++ in Docker). Source line: v1.0
// Canonical platform: sibling sdv-feature-flag-platform/src/Feature/src/EvEnergyFeature.cpp
// Version: argv[1] > DEMO_VERSION > kDefaultVersion

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
        if (battery < 5) battery = 85;

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
