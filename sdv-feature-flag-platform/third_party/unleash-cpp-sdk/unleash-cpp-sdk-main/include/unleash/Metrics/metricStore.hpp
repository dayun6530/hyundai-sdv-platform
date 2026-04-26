#pragma once
#include "unleash/Metrics/metricList.hpp"
#include "unleash/Configuration/clientConfig.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <optional>

namespace unleash {

class MetricsStore final {
  public:
    using clock = std::chrono::system_clock;

    MetricsStore(const ClientConfig& p_cfg);

    void reset();

    void addVariantMetric(const std::string& p_toggleName, bool p_isYes, const std::string& p_variantName);

    void addEnableMetric(const std::string& p_toggleName, bool p_isYes);

    bool empty() const;

    std::int64_t startTimestampMs() const;

    MetricList snapshot() const;

    std::optional<std::string> toJsonMetricsPayload() const;

  private:
    static std::int64_t nowMs();

  private:
    mutable std::mutex _mtx;
    MetricList _list;
    std::int64_t _startMs = 0;
    std::string _appName;
    std::string _instanceId;
};

} // namespace unleash