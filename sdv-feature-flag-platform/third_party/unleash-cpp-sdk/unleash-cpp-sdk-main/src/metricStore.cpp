#include "unleash/Metrics/metricStore.hpp"
#include "internal/jsonCodec.hpp"
#include "unleash/Utils/utils.hpp"
#include <cstdint>

namespace unleash {

MetricsStore::MetricsStore(const ClientConfig& p_cfg) {
    _startMs = nowMs();
    _appName = p_cfg.appName();
    _instanceId = p_cfg.instanceId();
}

void MetricsStore::reset() {
    std::lock_guard<std::mutex> g(_mtx);
    _list = MetricList{};
    _startMs = nowMs();
}

void MetricsStore::addVariantMetric(const std::string& p_toggleName, bool p_isYes, const std::string& p_variantName) {
    std::lock_guard<std::mutex> g(_mtx);
    _list.addVariantMetricData(p_toggleName, p_isYes, p_variantName);
}

void MetricsStore::addEnableMetric(const std::string& p_toggleName, bool p_isYes) {
    std::lock_guard<std::mutex> g(_mtx);
    _list.addEnableMetricData(p_toggleName, p_isYes);
}

bool MetricsStore::empty() const {
    std::lock_guard<std::mutex> g(_mtx);
    return _list.getList().empty();
}

std::int64_t MetricsStore::startTimestampMs() const {
    std::lock_guard<std::mutex> g(_mtx);
    return _startMs;
}

MetricList MetricsStore::snapshot() const {
    std::lock_guard<std::mutex> g(_mtx);
    return _list; // copy snapshot
}

std::optional<std::string> MetricsStore::toJsonMetricsPayload() const {
    MetricList snap;
    std::int64_t startMs = 0;
    std::int64_t stopMs = 0;

    {
        std::lock_guard<std::mutex> g(_mtx);

        if (_list.getList().empty())
            return std::nullopt;

        snap = _list;
        startMs = _startMs;
        stopMs = nowMs();
    }

    return JsonCodec::encodeMetricsRequestBody(snap, utils::fromMsTsToUtcTime(startMs),
                                               utils::fromMsTsToUtcTime(stopMs), _appName, _instanceId);
}

std::int64_t MetricsStore::nowMs() {
    const auto now = clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

} // namespace unleash