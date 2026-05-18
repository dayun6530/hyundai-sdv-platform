#pragma once

#include <chrono>
#include <string>

namespace sdv::platform::centercommunication {

/// POST JSON body; returns true on HTTP 2xx.
bool httpPostJson(const std::string& url, const std::string& body, std::string* error);

/// Retries with fixed backoff delays (same policy as legacy DataCollector OTLP export).
bool httpPostJsonWithRetry(const std::string& url,
    const std::string& body,
    const char* logPrefix,
    std::string* lastError);

}  // namespace sdv::platform::centercommunication
