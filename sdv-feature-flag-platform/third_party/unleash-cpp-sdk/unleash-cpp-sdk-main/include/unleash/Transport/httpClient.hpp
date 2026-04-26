#pragma once

#include "iComClient.hpp"
#include <curl/curl.h>
#include <atomic>
#include <map>
#include <string>
#include <functional>

namespace unleash {

struct HttpRequest : public IComRequest {
    std::string type() const override {
        return "http";
    }
    std::string url;
    bool usePOSTrequests = false;
    std::map<std::string, std::string> headers;
    std::string body;
    long timeoutMs = 0;
};

struct HttpResponse : public IComResponse {
    std::map<std::string, std::string> headers;
    std::string errorMessage;
};

class HttpClient : public IComClient {
  public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) = delete;
    HttpClient& operator=(HttpClient&&) = delete;

    std::unique_ptr<IComResponse> request(const IComRequest& req, CancelToken* cancel = nullptr) override;

  private:
    struct CurlSList {
        curl_slist* list = nullptr;
        ~CurlSList() {
            if (list)
                curl_slist_free_all(list);
        }
        void append(const char* str) {
            list = curl_slist_append(list, str);
        }
        operator curl_slist*() {
            return list;
        }
    };

    struct CurlHandle {
        CURL* curl;
        explicit CurlHandle() : curl(curl_easy_init()) {}
        ~CurlHandle() {
            if (curl)
                curl_easy_cleanup(curl);
        }
        operator CURL*() const {
            return curl;
        }
        bool valid() const {
            return curl != nullptr;
        }
    };

    void requestHttp(const HttpRequest& p_req, HttpResponse& p_resp, CancelToken* p_cancel = nullptr);

    static size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t headerCb(char* buffer, size_t size, size_t nitems, void* userdata);
    static int xferInfoCb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t);

    static std::string trimString(const std::string& str, const char* whitespace);
};

} // namespace unleash