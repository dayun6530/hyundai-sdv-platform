#pragma once
#include <string>
#include <optional>
#include "unleash/Configuration/clientConfig.hpp"
#include "unleash/Transport/httpClient.hpp"

namespace unleash {

class MetricSender final {

  public:
    struct MetricResult {
        int status = -1;
        std::optional<std::string> error;
    };

    MetricSender(const ClientConfig& p_config);

    MetricResult sendMetrics(const std::string& p_metricBody);

  private:
    void initializeHttpRequest(const ClientConfig& p_config);
    HttpClient _httpClient;
    HttpRequest _httpRequest;
};

} // namespace unleash
