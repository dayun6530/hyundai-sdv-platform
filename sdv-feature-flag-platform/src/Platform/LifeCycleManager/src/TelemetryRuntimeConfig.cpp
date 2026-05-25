#include "LifeCycleManager/TelemetryRuntimeConfig.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define SDV_TELEMETRY_RUNTIME_HAS_JSON 1
#endif

namespace sdv::platform::lifecyclemgr {

namespace {

constexpr std::size_t kBatchDefault = 50;
constexpr std::size_t kBatchMin = 1;
constexpr std::size_t kBatchMax = 100;
constexpr int kFlushDefault = 1000;
constexpr int kFlushMin = 500;
constexpr int kFlushMax = 2000;
constexpr int kPublishDefault = 1000;
constexpr int kPublishMin = 200;
constexpr int kPublishMax = 30000;

std::size_t clampBatch(std::size_t v) {
    return std::clamp(v, kBatchMin, kBatchMax);
}

int clampFlush(int v) {
    return std::clamp(v, kFlushMin, kFlushMax);
}

int clampPublish(int v) {
    return std::clamp(v, kPublishMin, kPublishMax);
}

std::filesystem::path repoRootFromLifecycleConfig() {
    const char* cfg = std::getenv("SDV_LIFECYCLE_CONFIG");
    if (!cfg || !*cfg) {
        return {};
    }
    std::filesystem::path p(cfg);
    std::error_code ec;
    if (!p.is_absolute()) {
        p = std::filesystem::absolute(p, ec);
    }
    for (int i = 0; i < 3 && p.has_parent_path(); ++i) {
        p = p.parent_path();
    }
    return p;
}

#if SDV_TELEMETRY_RUNTIME_HAS_JSON
nlohmann::json loadRuntimeJsonObject(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in.is_open()) {
        return nlohmann::json::object();
    }
    try {
        nlohmann::json j;
        in >> j;
        return j.is_object() ? j : nlohmann::json::object();
    } catch (...) {
        return nlohmann::json::object();
    }
}

void writeRuntimeJsonObject(const std::filesystem::path& p, const nlohmann::json& j) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p);
    if (!out.is_open()) {
        return;
    }
    out << j.dump(2) << '\n';
}
#endif

struct PublishCache {
    std::mutex mu;
    std::chrono::system_clock::time_point lastMtime{};
    std::string lastPath;
    int publishDefaultMs{kPublishDefault};
    std::map<std::string, int> byFeature;
};

PublishCache& publishCache() {
    static PublishCache c;
    return c;
}

std::string stripShadowSuffix(std::string name) {
    static constexpr const char* kSuffix = "Shadow";
    if (name.size() > 6 && name.compare(name.size() - 6, 6, kSuffix) == 0) {
        return name.substr(0, name.size() - 6);
    }
    return name;
}

}  // namespace

TelemetryCollectorTuning resolveTelemetryTuningFromEnv() {
    TelemetryCollectorTuning t;
    if (const char* env = std::getenv("SDV_TELEMETRY_BATCH_MAX")) {
        const long parsed = std::strtol(env, nullptr, 10);
        if (parsed >= static_cast<long>(kBatchMin) && parsed <= static_cast<long>(kBatchMax)) {
            t.batchMax = static_cast<std::size_t>(parsed);
        } else {
            t.batchMax = kBatchDefault;
        }
    } else {
        t.batchMax = kBatchDefault;
    }
    if (const char* env = std::getenv("SDV_TELEMETRY_FLUSH_MS")) {
        const long parsed = std::strtol(env, nullptr, 10);
        if (parsed >= kFlushMin && parsed <= kFlushMax) {
            t.flushMs = static_cast<int>(parsed);
        } else {
            t.flushMs = kFlushDefault;
        }
    } else {
        t.flushMs = kFlushDefault;
    }
    return t;
}

std::string resolveTelemetryRuntimeConfigPath(const std::string& carId) {
    if (const char* env = std::getenv("SDV_TELEMETRY_RUNTIME_CONFIG")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    const auto root = repoRootFromLifecycleConfig();
    if (!root.empty() && !carId.empty()) {
        return (root / "config" / "telemetry" / "runtime" / (carId + ".json")).string();
    }
    return "config/telemetry/runtime/" + carId + ".json";
}

bool tryReloadTelemetryRuntimeTuning(const std::string& configPath,
                                     TelemetryCollectorTuning& current,
                                     std::chrono::system_clock::time_point& lastReadTime) {
    if (configPath.empty()) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path p(configPath);
    if (!std::filesystem::is_regular_file(p, ec)) {
        return false;
    }
    const auto mtime = std::filesystem::last_write_time(p, ec);
    if (ec) {
        return false;
    }
    const auto mtimeSys =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            mtime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    if (mtimeSys == lastReadTime) {
        return false;
    }
    lastReadTime = mtimeSys;

    std::ifstream in(p);
    if (!in.is_open()) {
        return false;
    }

#if SDV_TELEMETRY_RUNTIME_HAS_JSON
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    if (!j.is_object()) {
        return false;
    }
    TelemetryCollectorTuning next = current;
    if (j.contains("batch_max") && j["batch_max"].is_number_integer()) {
        next.batchMax = clampBatch(static_cast<std::size_t>(j["batch_max"].get<long long>()));
    }
    if (j.contains("flush_ms") && j["flush_ms"].is_number_integer()) {
        next.flushMs = clampFlush(static_cast<int>(j["flush_ms"].get<long long>()));
    }
    if (next.batchMax == current.batchMax && next.flushMs == current.flushMs) {
        return false;
    }
    current = next;
    return true;
#else
    (void)current;
    return false;
#endif
}

int resolveTelemetryPublishIntervalMs(const std::string& featureName) {
    const char* carEnv = std::getenv("SDV_CAR_INSTANCE");
    const std::string carId = (carEnv && *carEnv) ? std::string(carEnv) : std::string("car_1");
    const std::string path = resolveTelemetryRuntimeConfigPath(carId);

#if !SDV_TELEMETRY_RUNTIME_HAS_JSON
    (void)featureName;
    (void)path;
    return kPublishDefault;
#else
    PublishCache& cache = publishCache();
    std::lock_guard<std::mutex> lock(cache.mu);

    std::error_code ec;
    const std::filesystem::path p(path);
    int publishDefault = kPublishDefault;
    std::map<std::string, int> byFeature;

    if (std::filesystem::is_regular_file(p, ec)) {
        const auto mtime = std::filesystem::last_write_time(p, ec);
        const auto mtimeSys =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                mtime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
        if (mtimeSys != cache.lastMtime || path != cache.lastPath) {
            cache.lastMtime = mtimeSys;
            cache.lastPath = path;
            const nlohmann::json j = loadRuntimeJsonObject(p);
            if (j.contains("publish_default_ms") && j["publish_default_ms"].is_number_integer()) {
                publishDefault = clampPublish(static_cast<int>(j["publish_default_ms"].get<long long>()));
            }
            if (j.contains("features") && j["features"].is_object()) {
                for (auto it = j["features"].begin(); it != j["features"].end(); ++it) {
                    if (!it.value().is_object()) {
                        continue;
                    }
                    if (it.value().contains("publish_interval_ms") &&
                        it.value()["publish_interval_ms"].is_number_integer()) {
                        byFeature[it.key()] = clampPublish(static_cast<int>(
                            it.value()["publish_interval_ms"].get<long long>()));
                    }
                }
            }
            cache.publishDefaultMs = publishDefault;
            cache.byFeature = byFeature;
        }
        publishDefault = cache.publishDefaultMs;
        byFeature = cache.byFeature;
    }

    auto lookup = [&](const std::string& name) -> std::optional<int> {
        const auto it = byFeature.find(name);
        if (it != byFeature.end()) {
            return it->second;
        }
        return std::nullopt;
    };

    if (auto v = lookup(featureName)) {
        return *v;
    }
    if (auto v = lookup(stripShadowSuffix(featureName))) {
        return *v;
    }
    return publishDefault;
#endif
}

void sleepTelemetryPublishInterval(const std::string& featureName) {
    const int ms = resolveTelemetryPublishIntervalMs(featureName);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace sdv::platform::lifecyclemgr
