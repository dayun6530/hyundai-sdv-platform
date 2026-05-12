#include "CenterCommunication/DataCollector.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>

#ifndef SDV_CENTERCOMM_HAS_LIBCURL
#define SDV_CENTERCOMM_HAS_LIBCURL 0
#endif

#if SDV_CENTERCOMM_HAS_LIBCURL
#include <curl/curl.h>
#else
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace {

#if SDV_CENTERCOMM_HAS_LIBCURL
struct CurlGlobalInit {
    CurlGlobalInit() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~CurlGlobalInit() {
        curl_global_cleanup();
    }
};

struct CurlHandle {
    CURL* handle = curl_easy_init();

    ~CurlHandle() {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
};

struct CurlHeaders {
    curl_slist* list = nullptr;

    ~CurlHeaders() {
        if (list) {
            curl_slist_free_all(list);
        }
    }
};

static CurlGlobalInit curlGlobalInit;

static std::size_t appendCurlResponse(char* ptr,
                                      std::size_t size,
                                      std::size_t nmemb,
                                      void* userdata) {
    const std::size_t total = size * nmemb;
    if (auto* response = static_cast<std::string*>(userdata)) {
        response->append(ptr, total);
    }
    return total;
}

static bool httpPostJson(const std::string& url, const std::string& body, std::string* error) {
    CurlHandle curl;
    if (!curl.handle) {
        if (error) *error = "curl_easy_init failed";
        return false;
    }

    CurlHeaders headers;
    headers.list = curl_slist_append(headers.list, "Content-Type: application/json");
    headers.list = curl_slist_append(headers.list, "Accept: application/json");
    if (!headers.list) {
        if (error) *error = "curl_slist_append failed";
        return false;
    }

    std::string responseBody;
    char errorBuffer[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_HTTPHEADER, headers.list);
    curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl.handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, appendCurlResponse);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);

    const CURLcode code = curl_easy_perform(curl.handle);
    if (code != CURLE_OK) {
        if (error) {
            *error = errorBuffer[0] != '\0'
                ? std::string(errorBuffer)
                : std::string(curl_easy_strerror(code));
        }
        return false;
    }

    long status = 0;
    curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300) {
        if (error) {
            *error = "HTTP status " + std::to_string(status);
            if (!responseBody.empty()) {
                *error += " body=" + responseBody;
            }
        }
        return false;
    }
    return true;
}
#else
struct ParsedHttpUrl {
    std::string host;
    std::string port;
    std::string path;
};

static bool parseHttpUrl(const std::string& url, ParsedHttpUrl& out) {
    static constexpr const char* kScheme = "http://";
    if (url.rfind(kScheme, 0) != 0) {
        return false;
    }
    const std::string rest = url.substr(std::strlen(kScheme));
    const std::size_t slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.empty()) {
        return false;
    }

    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        out.host = authority;
        out.port = "80";
        return !out.host.empty();
    }
    out.host = authority.substr(0, colon);
    out.port = authority.substr(colon + 1);
    return !out.host.empty() && !out.port.empty();
}

static bool sendAll(int fd, const std::string& data) {
    const char* p = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(fd, p, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        p += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

static bool httpPostJson(const std::string& url, const std::string& body, std::string* error) {
    ParsedHttpUrl parsed;
    if (!parseHttpUrl(url, parsed)) {
        if (error) *error = "unsupported or invalid URL: " + url;
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const int gai = ::getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &results);
    if (gai != 0) {
        if (error) *error = std::string("getaddrinfo failed: ") + ::gai_strerror(gai);
        return false;
    }

    int fd = -1;
    for (addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(results);

    if (fd < 0) {
        if (error) *error = "connect failed";
        return false;
    }

    std::ostringstream req;
    req << "POST " << parsed.path << " HTTP/1.1\r\n"
        << "Host: " << parsed.host << ":" << parsed.port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    if (!sendAll(fd, req.str())) {
        if (error) *error = "send failed";
        ::close(fd);
        return false;
    }

    std::string response;
    char buf[1024];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            response.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        break;
    }
    ::close(fd);

    if (response.rfind("HTTP/", 0) != 0 || response.size() < 12) {
        if (error) *error = "invalid HTTP response";
        return false;
    }
    const int status = std::atoi(response.substr(9, 3).c_str());
    if (status < 200 || status >= 300) {
        if (error) *error = "HTTP status " + std::to_string(status);
        return false;
    }
    return true;
}
#endif

static std::string getStringField(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

static void addStringAttribute(nlohmann::json& attributes,
                               const std::string& key,
                               const std::string& value) {
    if (key.empty() || value.empty()) {
        return;
    }
    attributes.push_back({
        {"key", key},
        {"value", {{"stringValue", value}}}
    });
}

static int severityNumberFromLevel(const std::string& level) {
    if (level == "TRACE") return 1;
    if (level == "DEBUG") return 5;
    if (level == "INFO") return 9;
    if (level == "WARN" || level == "WARNING") return 13;
    if (level == "ERROR") return 17;
    if (level == "FATAL") return 21;
    return 0;
}

static std::string iso8601UtcToUnixNano(const std::string& timestamp) {
    if (timestamp.size() < 20) {
        return {};
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(timestamp.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
                   &year, &month, &day, &hour, &minute, &second) != 6) {
        return {};
    }

    std::size_t pos = 19;
    std::uint64_t fractionalNanos = 0;
    if (pos < timestamp.size() && timestamp[pos] == '.') {
        ++pos;
        int digits = 0;
        while (pos < timestamp.size() && timestamp[pos] >= '0' && timestamp[pos] <= '9') {
            if (digits < 9) {
                fractionalNanos = fractionalNanos * 10 +
                    static_cast<std::uint64_t>(timestamp[pos] - '0');
                ++digits;
            }
            ++pos;
        }
        while (digits > 0 && digits < 9) {
            fractionalNanos *= 10;
            ++digits;
        }
    }

    if (pos >= timestamp.size() || timestamp[pos] != 'Z') {
        return {};
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;

    const std::time_t seconds = timegm(&tm);
    if (seconds < 0) {
        return {};
    }

    const std::uint64_t unixNanos =
        static_cast<std::uint64_t>(seconds) * 1000000000ULL + fractionalNanos;
    return std::to_string(unixNanos);
}

static std::string wrapAsOtlpLogJson(const std::string& carId, const std::string& telemetryJson) {
    nlohmann::json telemetry = nlohmann::json::object();
    try {
        telemetry = nlohmann::json::parse(telemetryJson);
    } catch (const nlohmann::json::parse_error&) {
        // Preserve the original body even if a producer emits malformed telemetry.
    }

    const std::string parsedCarId = getStringField(telemetry, "car_id");
    const std::string effectiveCarId = parsedCarId.empty() ? carId : parsedCarId;
    const std::string level = getStringField(telemetry, "level");
    const std::string timestampUnixNano =
        iso8601UtcToUnixNano(getStringField(telemetry, "timestamp"));

    nlohmann::json resourceAttributes = nlohmann::json::array();
    addStringAttribute(resourceAttributes, "service.name", "sdv-data-collector");
    addStringAttribute(resourceAttributes, "car_id", effectiveCarId);

    nlohmann::json logAttributes = nlohmann::json::array();
    addStringAttribute(logAttributes, "feature", getStringField(telemetry, "feature"));
    addStringAttribute(logAttributes, "event_type", getStringField(telemetry, "event_type"));
    addStringAttribute(logAttributes, "level", level);
    addStringAttribute(logAttributes, "role", getStringField(telemetry, "role"));

    nlohmann::json logRecord = {
        {"body", {{"stringValue", telemetryJson}}}
    };
    if (!timestampUnixNano.empty()) {
        logRecord["timeUnixNano"] = timestampUnixNano;
    }
    if (!level.empty()) {
        logRecord["severityText"] = level;
        const int severityNumber = severityNumberFromLevel(level);
        if (severityNumber > 0) {
            logRecord["severityNumber"] = severityNumber;
        }
    }
    if (!logAttributes.empty()) {
        logRecord["attributes"] = logAttributes;
    }

    nlohmann::json body = {
        {"resourceLogs", {{
            {"resource", {{"attributes", resourceAttributes}}},
            {"scopeLogs", {{
                {"scope", {{"name", "sdv-data-collector"}}},
                {"logRecords", {logRecord}}
            }}}
        }}}
    };
    return body.dump();
}

}  // namespace

namespace sdv::platform::centercommunication {

DataCollector::DataCollector(const std::string& eventBusAddress,
                             const std::string& otlpLogsEndpoint,
                             const std::string& carId)
    : eventBusAddress_(eventBusAddress)
    , otlpLogsEndpoint_(otlpLogsEndpoint)
    , carId_(carId)
    , running_(false) {}

void DataCollector::run() {
    running_ = true;

    sdv::platform::communications::eventbus::EventBusProxy proxy(eventBusAddress_);
    const int64_t cid = proxy.registerComponent();

    std::cout << "[DataCollector] Started. EventBus=" << eventBusAddress_
              << " component_id=" << cid
              << " otlpLogsEndpoint=" << otlpLogsEndpoint_
              << " carId=" << carId_
              << std::endl;

    while (running_) {
        // Poll EventBus for new events (pull model).
        auto q = proxy.getEventQueue(cid);
        while (q.getSize() > 0) {
            auto ev = q.getEvent();
            if (ev.getEventId() == sdv::platform::communications::eventbus::EventId::TelemetryData) {
                exportTelemetryPayload(ev.getMessage());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    proxy.unRegister(cid);
    std::cout << "[DataCollector] Stopped." << std::endl;
}

void DataCollector::stop() {
    running_ = false;
}

bool DataCollector::exportTelemetryPayload(const std::string& telemetryJson) {
    static constexpr int kMaxAttempts = 3;
    static constexpr std::chrono::milliseconds kBackoffDelays[] = {
        std::chrono::milliseconds(200),
        std::chrono::milliseconds(500)
    };

    const std::string body = wrapAsOtlpLogJson(carId_, telemetryJson);
    std::string lastError;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        std::string error;
        if (httpPostJson(otlpLogsEndpoint_, body, &error)) {
            if (attempt > 1) {
                std::cout << "[DataCollector] OTLP export recovered on attempt "
                          << attempt << "/" << kMaxAttempts << std::endl;
            }
            return true;
        }

        lastError = error;
        if (attempt < kMaxAttempts) {
            const auto delay = kBackoffDelays[attempt - 1];
            std::cerr << "[DataCollector] WARN: OTLP export attempt "
                      << attempt << "/" << kMaxAttempts << " failed to "
                      << otlpLogsEndpoint_ << " (" << error
                      << "); retrying in " << delay.count() << "ms" << std::endl;
            std::this_thread::sleep_for(delay);
        }
    }

    std::cerr << "[DataCollector] WARN: failed to export OTLP log to "
              << otlpLogsEndpoint_ << " after " << kMaxAttempts
              << " attempts (" << lastError << ")" << std::endl;
    return false;
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void dataCollectorSignalHandler(int) {
    shutdownRequested = true;
}

#if !defined(SDV_CENTERCOMM_NO_MAIN)
static std::string resolveEventBusAddressFromEnvOrDefault() {
    if (const char* env = std::getenv("SDV_EVENTBUS_ADDR"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "127.0.0.1:50051";
}

static std::string resolveOtlpLogsEndpointFromEnvOrDefault() {
    if (const char* env = std::getenv("SDV_OTLP_LOGS_ENDPOINT"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "http://127.0.0.1:4318/v1/logs";
}

static std::string resolveCarIdFromArgvOrEnv(int argc, char** argv) {
    if (argc > 1 && argv[argc - 1]) {
        const std::string last = std::string(argv[argc - 1]);
        if (last.rfind("car_", 0) == 0) {
            return last;
        }
    }
    if (const char* env = std::getenv("SDV_CAR_INSTANCE"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    return "car_1";
}

int main(int argc, char** argv) {
    signal(SIGTERM, dataCollectorSignalHandler);
    signal(SIGINT, dataCollectorSignalHandler);

    std::string eventBusAddr = resolveEventBusAddressFromEnvOrDefault();
    if (argc > 1 && argv[1] && *argv[1] != '\0') {
        eventBusAddr = std::string(argv[1]);
    }

    const std::string otlpLogsEndpoint = resolveOtlpLogsEndpointFromEnvOrDefault();
    const std::string carId = resolveCarIdFromArgvOrEnv(argc, argv);
    sdv::platform::centercommunication::DataCollector collector(
        eventBusAddr, otlpLogsEndpoint, carId);

    std::thread collectorThread([&collector]() {
        collector.run();
    });

    while (!shutdownRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    collector.stop();
    collectorThread.join();

    return 0;
}
#endif
