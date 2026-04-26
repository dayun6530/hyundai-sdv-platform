#pragma once
#include <string>
#include <utility>
#include <map>

namespace unleash {

class MetricToggle final {

  public:
    explicit MetricToggle(const std::string& p_toggleName, bool p_isYes);
    explicit MetricToggle(const std::string& p_toggleName, bool p_isYes, const std::string& p_variantName);

    const std::string& getToggleName() const;
    const std::map<std::string, unsigned int>& getVariantStats() const;
    unsigned int getYesCount() const;
    unsigned int getNoCount() const;

    void updateEnableMetric(bool p_isYes);
    void updateVariantMetric(bool p_isYes, const std::string& p_variantName);

  private:
    std::string _toggleName;
    std::map<std::string, unsigned int> _variantsStats;
    unsigned int _yesCount = 0;
    unsigned int _noCount = 0;
};

} // namespace unleash