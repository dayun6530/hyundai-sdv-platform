// Standalone OTA demo (single-file g++ in Docker). Source line: v1.0
// Canonical platform: sibling sdv-feature-flag-platform/src/Feature/src/NavigationFeature.cpp
// Version: argv[1] > DEMO_VERSION > kDefaultVersion

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char* kFeatureName = "NavigationFeature";
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
    std::cout << "[SYSTEM] Navigation daemon entering broadcast loop.\n";
    std::cout.flush();

    int tick = 0;
    while (true) {
        if (std::strcmp(role, "shadow") == 0) {
            std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                      << ") running [SHADOW observation]\n";
            std::cout << "[NAV] tick=" << (++tick)
                      << " | route=stable | guidance=OK | lane-keep=shadow | eta=+00:00:03\n";
        } else {
            std::cout << "[SYSTEM] " << kFeatureName << " (Version: " << version
                      << ") running [PRIMARY production]\n";
            std::cout << "[NAV] tick=" << (++tick)
                      << " | route=stable | guidance=OK | lane-keep=live | eta=+00:00:03\n";
        }
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
