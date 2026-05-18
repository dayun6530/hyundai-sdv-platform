#include "CenterCommunication/telemetry/HttpPostJson.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

#ifndef SDV_CENTERCOMM_HAS_LIBCURL
#define SDV_CENTERCOMM_HAS_LIBCURL 0
#endif

#if SDV_CENTERCOMM_HAS_LIBCURL
#include <curl/curl.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sdv::platform::centercommunication {

namespace {

#if SDV_CENTERCOMM_HAS_LIBCURL
struct CurlGlobalInit {
    CurlGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInit() { curl_global_cleanup(); }
};

struct CurlHandle {
    CURL* handle = curl_easy_init();
    ~CurlHandle() {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
};

struct CurlHeaders {
    curl_slist* list = nullptr;
    ~CurlHeaders() {
        if (list) {
            curl_slist_free_all(list);
        }
    }
};

static CurlGlobalInit curlGlobalInit;

static std::size_t appendCurlResponse(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    const std::size_t total = size * nmemb;
    if (auto* response = static_cast<std::string*>(userdata)) {
        response->append(ptr, total);
    }
    return total;
}
#else
struct ParsedHttpUrl {
    std::string host;
    std::string port;
    std::string path;
};

static bool parseHttpUrl(const std::string& url, ParsedHttpUrl& out) {
    static constexpr const char* kScheme = "http://";
    if (url.rfind(kScheme, 0) != 0) {
        return false;
    }
    const std::string rest = url.substr(std::strlen(kScheme));
    const std::size_t slash = rest.find('/');
    const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.empty()) {
        return false;
    }

    const std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        out.host = authority;
        out.port = "80";
        return !out.host.empty();
    }
    out.host = authority.substr(0, colon);
    out.port = authority.substr(colon + 1);
    return !out.host.empty() && !out.port.empty();
}

static bool sendAll(int fd, const std::string& data) {
    const char* p = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t sent = ::send(fd, p, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        p += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}
#endif

}  // namespace

bool httpPostJson(const std::string& url, const std::string& body, std::string* error) {
#if SDV_CENTERCOMM_HAS_LIBCURL
    CurlHandle curl;
    if (!curl.handle) {
        if (error) {
            *error = "curl_easy_init failed";
        }
        return false;
    }

    CurlHeaders headers;
    headers.list = curl_slist_append(headers.list, "Content-Type: application/json");
    headers.list = curl_slist_append(headers.list, "Accept: application/json");
    if (!headers.list) {
        if (error) {
            *error = "curl_slist_append failed";
        }
        return false;
    }

    std::string responseBody;
    char errorBuffer[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_HTTPHEADER, headers.list);
    curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl.handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, appendCurlResponse);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);

    const CURLcode code = curl_easy_perform(curl.handle);
    if (code != CURLE_OK) {
        if (error) {
            *error = errorBuffer[0] != '\0' ? std::string(errorBuffer)
                                          : std::string(curl_easy_strerror(code));
        }
        return false;
    }

    long status = 0;
    curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300) {
        if (error) {
            *error = "HTTP status " + std::to_string(status);
            if (!responseBody.empty()) {
                *error += " body=" + responseBody;
            }
        }
        return false;
    }
    return true;
#else
    ParsedHttpUrl parsed;
    if (!parseHttpUrl(url, parsed)) {
        if (error) {
            *error = "unsupported or invalid URL: " + url;
        }
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const int gai = ::getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &results);
    if (gai != 0) {
        if (error) {
            *error = std::string("getaddrinfo failed: ") + ::gai_strerror(gai);
        }
        return false;
    }

    int fd = -1;
    for (addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(results);

    if (fd < 0) {
        if (error) {
            *error = "connect failed";
        }
        return false;
    }

    std::ostringstream req;
    req << "POST " << parsed.path << " HTTP/1.1\r\n"
        << "Host: " << parsed.host << ":" << parsed.port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    if (!sendAll(fd, req.str())) {
        if (error) {
            *error = "send failed";
        }
        ::close(fd);
        return false;
    }

    std::string response;
    char buf[1024];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            response.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        break;
    }
    ::close(fd);

    if (response.rfind("HTTP/", 0) != 0 || response.size() < 12) {
        if (error) {
            *error = "invalid HTTP response";
        }
        return false;
    }
    const int status = std::atoi(response.substr(9, 3).c_str());
    if (status < 200 || status >= 300) {
        if (error) {
            *error = "HTTP status " + std::to_string(status);
        }
        return false;
    }
    return true;
#endif
}

bool httpPostJsonWithRetry(const std::string& url,
    const std::string& body,
    const char* logPrefix,
    std::string* lastError) {
    static constexpr int kMaxAttempts = 3;
    static constexpr std::chrono::milliseconds kBackoffDelays[] = {
        std::chrono::milliseconds(200),
        std::chrono::milliseconds(500),
    };

    const char* prefix = (logPrefix != nullptr && logPrefix[0] != '\0') ? logPrefix : "HttpPost";

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        std::string error;
        if (httpPostJson(url, body, &error)) {
            if (attempt > 1) {
                std::cout << "[" << prefix << "] export recovered on attempt " << attempt << "/"
                          << kMaxAttempts << std::endl;
            }
            return true;
        }

        if (lastError != nullptr) {
            *lastError = error;
        }
        if (attempt < kMaxAttempts) {
            const auto delay = kBackoffDelays[attempt - 1];
            std::cerr << "[" << prefix << "] WARN: attempt " << attempt << "/" << kMaxAttempts
                      << " failed to " << url << " (" << error << "); retrying in "
                      << delay.count() << "ms" << std::endl;
            std::this_thread::sleep_for(delay);
        }
    }

    std::cerr << "[" << prefix << "] WARN: failed after " << kMaxAttempts << " attempts to " << url;
    if (lastError != nullptr && !lastError->empty()) {
        std::cerr << " (" << *lastError << ")";
    }
    std::cerr << std::endl;
    return false;
}

}  // namespace sdv::platform::centercommunication
