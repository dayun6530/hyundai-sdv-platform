#pragma once
#include <string_view>
#include <string>
#include <variant>
#include <cstdint>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <random>
#include <algorithm>
#include <cstring>

#ifndef UNLEASH_SDK_VERSION
#define UNLEASH_SDK_VERSION "1.0.0-dev"
#endif

namespace utils {
inline constexpr std::string_view sdkVersion = "unleash-cpp-sdk:" UNLEASH_SDK_VERSION;
inline constexpr std::string_view agentVersion = "unleash-cpp-sdk/" UNLEASH_SDK_VERSION;
inline constexpr std::string_view metricsExtansion = "/client/metrics";
inline constexpr std::string_view defaultAppName = "unleash-client-app";
inline constexpr std::string_view defaultHeadeName = "authorization";
inline constexpr std::string_view defaultInstanceId = "cppApplication";

inline constexpr int httpStatusOkLower = 200;
inline constexpr int httpStatusOkUpper = 300;
inline constexpr int httpStatusNoUpdate = 304;

using contextProperties = std::vector<std::pair<std::string, std::string>>;
using seconds = std::chrono::seconds;
using mSeconds = std::chrono::milliseconds;

inline constexpr unsigned int maxEventQueueSize = 30;

inline std::string keysToLowerCase(std::string p_key) {
    std::transform(p_key.begin(), p_key.end(), p_key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return p_key;
}

static std::string fromMsTsToUtcTime(std::int64_t p_msTimeStamp) {
    // Get second and fraction milliseconds:
    std::int64_t sec = p_msTimeStamp / 1000;
    std::int64_t ms = p_msTimeStamp % 1000;
    if (ms < 0) {
        ms += 1000;
        --sec;
    }

    std::time_t t = static_cast<std::time_t>(sec);

    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
    return oss.str();
}

static std::string getISO8601CurrentTimeStamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    long offset_seconds;
#ifdef _WIN32
    _get_timezone(&offset_seconds);
    offset_seconds = -offset_seconds;
    if (local_tm.tm_isdst > 0) {
        offset_seconds += 3600;
    }
#else
    offset_seconds = local_tm.tm_gmtoff;
#endif

    int offset_hours = offset_seconds / 3600;
    int offset_minutes = std::abs((offset_seconds % 3600) / 60);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%dT%H:%M:%S");
    oss << (offset_hours >= 0 ? '+' : '-') << std::setfill('0') << std::setw(2) << std::abs(offset_hours) << ':'
        << std::setfill('0') << std::setw(2) << offset_minutes;

    return oss.str();
}

static std::string uuidv4Generator() {
    static constexpr char hex[] = "0123456789abcdef";
    std::string s = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";

    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);

    std::transform(s.begin(), s.end(), s.begin(), [&](char c) {
        if (c == 'x')
            return hex[dist(gen)];
        if (c == 'y')
            return hex[(dist(gen) & 0x3) | 0x8];
        return c;
    });

    return s;
}

} // namespace utils