#include "unleash/Metrics/metricSender.hpp"
#include "unleash/Utils/utils.hpp"

namespace unleash {

MetricSender::MetricSender(const ClientConfig& p_config) {
    initializeHttpRequest(p_config);
}

MetricSender::MetricResult MetricSender::sendMetrics(const std::string& p_metricBody) {
    _httpRequest.body = p_metricBody;
    auto resp = _httpClient.request(_httpRequest);
    MetricSender::MetricResult result;

    if (!resp) {
        result.error = "Error: null response from HttpClient";
        return result;
    }

    auto httpError = dynamic_cast<ErrorResponse*>(resp.get());
    if (httpError) {
        std::string errorMessage = "Request failed with code error <" +
                                   std::to_string(static_cast<int>(httpError->code())) + "> and message:\n " +
                                   httpError->message();

        result.error = std::move(errorMessage);
        return result;
    }
    auto httpResponse = dynamic_cast<HttpResponse*>(resp.get());
    if (!httpResponse) {
        std::string errorMessage = "Error: The resulting response is not of type: HttpResponse.";
        result.error = std::move(errorMessage);

        return result;
    }
    // Transport-level error...
    if (httpResponse->status == 0) {
        result.status = httpResponse->status;
        std::string errorMessage = "Transport error (status=0). Network failure / DNS / connection refused / etc.";
        result.error = std::move(errorMessage);
        return result;
    }

    result.status = httpResponse->status;
    if (httpResponse->status >= utils::httpStatusOkLower && httpResponse->status < utils::httpStatusOkUpper) {
        // all ok!
        return result;
    }
    result.error = "HTTP error status=" + std::to_string(httpResponse->status) + " body=" + httpResponse->body +
                   " Error: " + httpResponse->errorMessage;
    return result;
}

void MetricSender::initializeHttpRequest(const ClientConfig& p_config) {
    _httpRequest.url = p_config.url() + "/client/metrics";
    _httpRequest.usePOSTrequests = true;
    _httpRequest.timeoutMs = static_cast<long>(p_config.timeOutQueryMS().count());
    _httpRequest.headers.clear();

    _httpRequest.headers["accept"] = "application/json";
    _httpRequest.headers["unleash-connection-id"] = p_config.connectionId();
    _httpRequest.headers[utils::keysToLowerCase(p_config.headerName())] = p_config.clientKey();
    _httpRequest.headers["unleash-sdk"] = std::string(utils::sdkVersion);
    _httpRequest.headers["user-agent"] = std::string(utils::agentVersion);
    if (_httpRequest.usePOSTrequests) {
        _httpRequest.headers["content-type"] = "application/json";
    }
    _httpRequest.headers["unleash-appname"] = p_config.appName();

    for (const auto& [k, v] : p_config.customHeaders()) {
        if (k.empty())
            continue;
        _httpRequest.headers[utils::keysToLowerCase(k)] = v;
    }
}

} // namespace unleash