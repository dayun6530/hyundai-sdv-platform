#include "unleash/Transport/httpClient.hpp"
#include <iostream>
#include <cctype>
#include <algorithm>

namespace unleash {

namespace {

struct CurlGlobalInit {
    CurlGlobalInit() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobalInit() {
        curl_global_cleanup();
    }
};

void ensureCurlInit() {
    static CurlGlobalInit init;
}

} // namespace

HttpClient::HttpClient() {
    ensureCurlInit();
}

HttpClient::~HttpClient() {
    // Cleanup handled by singleton
}

std::unique_ptr<IComResponse> HttpClient::request(const IComRequest& p_req, CancelToken* p_cancel) {
    // Use dynamic_cast directly - it checks type compatibility
    auto const* httpReq = dynamic_cast<const HttpRequest*>(&p_req);
    if (!httpReq) {
        auto err =
            std::make_unique<ErrorResponse>(ComErrorEnum::TypeMismatch, "The request is not an HttpRequest type.");
        return err;
    }

    auto httpResp = std::make_unique<HttpResponse>();
    requestHttp(*httpReq, *httpResp, p_cancel);

    return httpResp;
}

void HttpClient::requestHttp(const HttpRequest& p_req, HttpResponse& p_resp, CancelToken* p_cancel) {
    CurlHandle curlHandle;
    if (!curlHandle.valid()) {
        p_resp.status = -1;
        p_resp.errorMessage = "Failed to initialize CURL";
        return;
    }

    CURL* curl = curlHandle;

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, p_req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Set HTTP method and body
    if (p_req.usePOSTrequests) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, p_req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, p_req.body.size());
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    // Set timeout
    if (p_req.timeoutMs > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, p_req.timeoutMs);
    }

    // Set headers
    CurlSList hdrs;
    for (const auto& [k, v] : p_req.headers) {
        std::string line = k + ": " + v;
        hdrs.append(line.c_str());
    }
    if (hdrs.list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs.list);
    }

    // Set response body callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &p_resp.body);

    // Set response header callback
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &headerCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &p_resp.headers);

    // Set progress callback for cancellation
    if (p_cancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &xferInfoCb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, p_cancel);
    }

    // Perform the curl operation
    CURLcode code = curl_easy_perform(curl);

    if (code == CURLE_OK) {
        long statusCode = 0;
        if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode) == CURLE_OK) {
            p_resp.status = statusCode;
        } else {
            p_resp.status = -1;
            p_resp.errorMessage = "Failed to retrieve response code";
        }
    } else {
        p_resp.status = -1;
        p_resp.errorMessage = curl_easy_strerror(code);
    }
}

size_t HttpClient::writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    size_t totalSize = size * nmemb;
    out->append(ptr, totalSize);
    return totalSize;
}

size_t HttpClient::headerCb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t totalSize = size * nitems;
    std::string line(buffer, totalSize);

    auto pos = line.find(':');
    if (pos != std::string::npos) {
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        // Trim whitespace efficiently
        val = trimString(val, " \t\r\n");

        // Convert key to lowercase
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });

        if (!key.empty() && !val.empty()) {
            (*headers)[key] = val;
        }
    }
    return totalSize;
}

int HttpClient::xferInfoCb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* flag = static_cast<std::atomic<bool>*>(clientp);
    // Return non-zero to abort transfer
    return flag->load() ? 1 : 0;
}

std::string HttpClient::trimString(const std::string& str, const char* whitespace) {
    const size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return ""; // String is all whitespace
    }

    const size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

} // namespace unleash