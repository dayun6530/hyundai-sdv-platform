#include "unleash/Metrics/metricList.hpp"

namespace unleash {

const MetricList::MetricsMap& MetricList::getList() const {
    return _metricList;
}

void MetricList::addEnableMetricData(const std::string& p_toggleName, bool p_isYes) {
    auto it = _metricList.find(p_toggleName);
    if (it != _metricList.end()) {
        it->second.updateEnableMetric(p_isYes);
        return;
    }
    _metricList.emplace(p_toggleName, MetricToggle(p_toggleName, p_isYes));
}

void MetricList::addVariantMetricData(const std::string& p_toggleName, bool p_isYes, const std::string& p_variantName) {
    auto it = _metricList.find(p_toggleName);
    if (it != _metricList.end()) {
        it->second.updateVariantMetric(p_isYes, p_variantName);
        return;
    }
    _metricList.emplace(p_toggleName, MetricToggle(p_toggleName, p_isYes, p_variantName));
}

} // namespace unleash