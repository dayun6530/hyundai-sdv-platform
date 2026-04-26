#include "unleash/Metrics/metricToggle.hpp"

namespace unleash {
MetricToggle::MetricToggle(const std::string& p_toggleName, bool p_isYes) : _toggleName(p_toggleName) {
    updateEnableMetric(p_isYes);
}

MetricToggle::MetricToggle(const std::string& p_toggleName, bool p_isYes, const std::string& p_variantName)
    : _toggleName(p_toggleName) {

    updateVariantMetric(p_isYes, p_variantName);
}

const std::string& MetricToggle::getToggleName() const {
    return _toggleName;
}

const std::map<std::string, unsigned int>& MetricToggle::getVariantStats() const {
    return _variantsStats;
}

unsigned int MetricToggle::getYesCount() const {
    return _yesCount;
}

unsigned int MetricToggle::getNoCount() const {
    return _noCount;
}

void MetricToggle::updateEnableMetric(bool p_isYes) {
    if (p_isYes)
        ++_yesCount;
    else
        ++_noCount;
}

void MetricToggle::updateVariantMetric(bool p_isYes, const std::string& p_variantName) {
    if (p_isYes)
        ++_yesCount;
    else
        ++_noCount;
    if (!p_variantName.empty())
        ++_variantsStats[p_variantName];
}

} // namespace unleash