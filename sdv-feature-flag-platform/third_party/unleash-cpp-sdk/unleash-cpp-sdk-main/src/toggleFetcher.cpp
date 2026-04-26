#include "unleash/Fetcher/toggleFetcher.hpp"
#include "unleash/Utils/utils.hpp"
#include "internal/jsonCodec.hpp"
#include <cctype>
#include <sstream>
#include <iomanip>

namespace unleash {

ToggleFetcher::ToggleFetcher(const ClientConfig& p_config) {
    makeFrontendRequest(p_config);
}

void ToggleFetcher::makeFrontendRequest(const ClientConfig& p_config) {
    _baseUrl = p_config.url();
    _httpRequest.url = _baseUrl;
    _httpRequest.usePOSTrequests = p_config.usePostRequests();
    _httpRequest.timeoutMs = static_cast<long>(p_config.timeOutQueryMS().count());
    // Fill headers similar to JS parseHeaders
    _httpRequest.headers.clear();
    _httpRequest.headers["accept"] = "application/json";
    _httpRequest.headers["'unleash-connection-id'"] = p_config.connectionId();
    // Authorization header (configurable name)
    _httpRequest.headers[utils::keysToLowerCase(p_config.headerName())] = p_config.clientKey();

    _httpRequest.headers["unleash-sdk"] = std::string(utils::sdkVersion);
    _httpRequest.headers["user-agent"] = std::string(utils::agentVersion);

    _httpRequest.headers["unleash-appname"] = p_config.appName();

    if (_httpRequest.usePOSTrequests) {
        _httpRequest.headers["content-type"] = "application/json";
    }
    if (!_etag.empty())
        _httpRequest.headers["if-none-match"] = _etag;
    // Custom headers (override defaults if same key)
    for (const auto& [k, v] : p_config.customHeaders()) {
        if (k.empty())
            continue;
        _httpRequest.headers[utils::keysToLowerCase(k)] = v;
    }
}

namespace {

std::string urlEncodeContext(std::string_view value) {
    std::string out;
    out.reserve(value.size() * 3);

    static constexpr char hex[] = "0123456789ABCDEF";

    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }

    return out;
}

std::string buildContextQuery(const Context& ctx) {
    std::string query;
    query.reserve(256);

    auto add = [&](const std::string& k, const std::string& v) {
        if (v.empty())
            return;
        const char sep = query.empty() ? '?' : '&';
        query.push_back(sep);
        query.append(urlEncodeContext(k));
        query.push_back('=');
        query.append(urlEncodeContext(v));
    };

    add("appName", ctx.getAppName());
    add("sessionId", ctx.getSessionId());
    if (ctx.getEnvironment())
        add("environment", *ctx.getEnvironment());
    if (ctx.getUserId())
        add("userId", *ctx.getUserId());
    if (ctx.getRemoteAddress())
        add("remoteAddress", *ctx.getRemoteAddress());
    if (ctx.getCurrentTime())
        add("currentTime", *ctx.getCurrentTime());

    for (const auto& [k, v] : ctx.getProperties()) {
        if (k.empty() || v.empty())
            continue;
        // Encode custom properties using properties.<key>
        add("properties[" + k + "]", v);
    }

    return query;
}

} // namespace

ToggleFetcher::FetchResult ToggleFetcher::fetch(const Context& p_ctx) {
    if (_httpRequest.usePOSTrequests) {
        _httpRequest.body = JsonCodec::encodeContextRequestBody(p_ctx);
    } else {
        _httpRequest.body.clear();
        _httpRequest.url = _baseUrl;
        _httpRequest.url += buildContextQuery(p_ctx);
    }
    auto resp = _httpClient.request(_httpRequest);
    FetchResult result;

    if (!resp) {
        result.error = "Error: null response from HttpClient";
        return result;
    }

    // verify if it's an error response:
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
    // No modification case:
    if (httpResponse->status == utils::httpStatusNoUpdate) {
        return result;
    }

    if (httpResponse->status >= utils::httpStatusOkLower && httpResponse->status < utils::httpStatusOkUpper) {
        auto toggleSet = JsonCodec::decodeClientFeaturesResponse(httpResponse->body);
        if (!toggleSet.has_value()) {
            result.error = "Failed to decode toggles JSON: " + toggleSet.error();
            return result;
        }
        if (toggleSet->size()) {
            result.toggles = std::move(toggleSet.value());
        }
        auto it = httpResponse->headers.find("etag");
        if (it != httpResponse->headers.end() && !it->second.empty()) {
            _etag = it->second;
            _httpRequest.headers["if-none-match"] = _etag;
        }
        return result;
    }
    result.error = "Error: " + httpResponse->errorMessage;
    return result;
}

} // namespace unleash
