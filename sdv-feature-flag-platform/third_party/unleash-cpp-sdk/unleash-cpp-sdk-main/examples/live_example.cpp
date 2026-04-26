#include "unleash/Client/unleashClient.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main() {
    using namespace std::chrono_literals;

    const char* url = std::getenv("UNLEASH_URL");
    const char* apiKey = std::getenv("UNLEASH_API_KEY");
    const char* featureName = std::getenv("UNLEASH_FEATURE_NAME");

    if (url == nullptr || apiKey == nullptr) {
        std::cerr << "Set UNLEASH_URL and UNLEASH_API_KEY before running this example." << std::endl;
        return 1;
    }

    std::string flag = featureName ? featureName : "demo-flag";

    unleash::ClientConfig config(url, apiKey, "cpp-live-example");
    config.setRefreshInterval(std::chrono::seconds{1})
        .setMetricsInterval(std::chrono::seconds{2})
        .setMetricsIntervalInitial(std::chrono::seconds{2});

    unleash::Context context("cpp-live-example", "development");
    context.setUserId("123");
    context.setProperty("tennantId", "tennant-123");

    unleash::UnleashClient client(std::move(config), std::move(context));

    client.onInit([]() { std::cout << "[init] client starting" << std::endl; });

    client.onReady([]() { std::cout << "[ready] first successful fetch completed" << std::endl; });

    client.onUpdate([]() { std::cout << "[update] toggles changed" << std::endl; });

    client.onError([](const unleash::EventHandler::ClientError& e) {
        std::cerr << "[error] " << e.message << ": " << e.details << std::endl;
    });

    client.start();

    while (true) {
        std::this_thread::sleep_for(1s);

        std::cout << "Flag '" << flag << "' enabled: " << std::boolalpha << client.isEnabled(flag);

        const auto variant = client.getVariant(flag);
        std::cout << "  variant: " << variant.name();

        std::cout << std::endl;
    }

    return 0;
}
