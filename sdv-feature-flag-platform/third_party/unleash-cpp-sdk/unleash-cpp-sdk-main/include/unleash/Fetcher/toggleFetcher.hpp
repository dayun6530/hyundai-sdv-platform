#pragma once
#include "unleash/Domain/toggleSet.hpp"
#include "unleash/Configuration/clientConfig.hpp"
#include "unleash/Transport/httpClient.hpp"
#include <memory>
#include <algorithm>
#include <cctype>
#include <string>
#include <optional>

namespace unleash {

class ToggleFetcher {
  public:
    struct FetchResult {
        int status = -1;
        std::optional<ToggleSet> toggles = std::nullopt;
        std::optional<std::string> error;
    };

    ToggleFetcher(const ClientConfig& p_config);

    FetchResult fetch(const Context& p_ctx);

    const HttpRequest& getHttpRequest() const {
        return _httpRequest;
    }

  private:
    void makeFrontendRequest(const ClientConfig& p_config);

    HttpClient _httpClient;
    HttpRequest _httpRequest;
    std::string _baseUrl;
    std::string _etag;
};

} // namespace unleash
