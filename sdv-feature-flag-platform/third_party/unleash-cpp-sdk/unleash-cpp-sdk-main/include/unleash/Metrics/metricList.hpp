#pragma once
#include <string>
#include <utility>
#include <map>
#include "unleash/Metrics/metricToggle.hpp"

namespace unleash {

class MetricList final {

  public:
    using MetricsMap = std::map<std::string, MetricToggle>;
    explicit MetricList() = default;

    const MetricsMap& getList() const;
    void addEnableMetricData(const std::string& p_toggleName, bool p_isYes);
    void addVariantMetricData(const std::string& p_toggleName, bool p_isYes, const std::string& p_variantName);

  private:
    MetricsMap _metricList;
};

} // namespace unleash