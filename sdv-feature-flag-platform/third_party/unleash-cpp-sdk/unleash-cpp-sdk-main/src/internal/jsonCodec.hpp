#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "expected.hpp"
#include "unleash/Domain/context.hpp"
#include "unleash/Domain/toggleSet.hpp"
#include "unleash/Metrics/metricList.hpp"

namespace unleash {

class JsonCodec {
  public:
    using json = nlohmann::json;

    static internal::Expected<ToggleSet, std::string> decodeClientFeaturesResponse(const std::string& jsonText);

    static std::string encodeClientFeaturesResponse(const ToggleSet& toggleSet);

    static std::string encodeContextRequestBody(const Context& ctx);

    static std::string encodeMetricsRequestBody(const MetricList& p_metricList, const std::string& p_start,
                                                const std::string& p_end, const std::string& p_appName,
                                                const std::string& p_instanceId);
};

} // namespace unleash
