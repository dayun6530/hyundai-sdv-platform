#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "unleash/Transport/httpClient.hpp"
#include "unleash/Transport/iComClient.hpp"

using namespace unleash;

// -----------------------------
// Tiny local HTTP server
// -----------------------------
// Cross-platform minimal TCP server supporting:
// - GET /etag   : returns 200 with ETag unless If-None-Match matches, then 304
// - POST /post  : returns 200 and echoes request body
//

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socklen_t = int;
static void closesock(SOCKET s) {
    closesocket(s);
}
static bool socket_valid(SOCKET s) {
    return s != INVALID_SOCKET;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
static void closesock(SOCKET s) {
    close(s);
}
static bool socket_valid(SOCKET s) {
    return s >= 0;
}
#endif

namespace {

struct WinsockRAII {
#ifdef _WIN32
    WSADATA wsa{};
    WinsockRAII() {
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockRAII() {
        WSACleanup();
    }
#else
    WinsockRAII() = default;
    ~WinsockRAII() = default;
#endif
};

static std::string toLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static void trimInPlace(std::string& s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back())))
        s.pop_back();
}

struct ParsedRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers; // lowercase keys
    std::string body;
};

static ParsedRequest parseHttpRequest(const std::string& raw) {
    ParsedRequest r;
    std::istringstream iss(raw);

    std::string requestLine;
    std::getline(iss, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r')
        requestLine.pop_back();

    {
        std::istringstream rl(requestLine);
        rl >> r.method;
        rl >> r.path;
    }

    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        trimInPlace(k);
        trimInPlace(v);
        r.headers[toLower(k)] = v;
    }

    std::ostringstream body;
    body << iss.rdbuf();
    r.body = body.str();

    return r;
}

static std::string httpResponse(long status, const std::vector<std::pair<std::string, std::string>>& headers,
                                const std::string& body) {
    std::ostringstream oss;
    if (status == 200)
        oss << "HTTP/1.1 200 OK\r\n";
    else if (status == 304)
        oss << "HTTP/1.1 304 Not Modified\r\n";
    else
        oss << "HTTP/1.1 " << status << " \r\n";

    for (auto& h : headers) {
        oss << h.first << ": " << h.second << "\r\n";
    }
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n\r\n";
    oss << body;
    return oss.str();
}

class TinyHttpServer {
  public:
    TinyHttpServer() {
        (void)_wsa;

        _listenSock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (!socket_valid(_listenSock)) {
            throw std::runtime_error("socket() failed");
        }

        int yes = 1;
#ifdef _WIN32
        setsockopt(_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
        setsockopt(_listenSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        if (::bind(_listenSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
            closesock(_listenSock);
            throw std::runtime_error("bind() failed");
        }

        socklen_t len = sizeof(addr);
        if (::getsockname(_listenSock, (sockaddr*)&addr, &len) != 0) {
            closesock(_listenSock);
            throw std::runtime_error("getsockname() failed");
        }
        _port = ntohs(addr.sin_port);

        if (::listen(_listenSock, 8) != 0) {
            closesock(_listenSock);
            throw std::runtime_error("listen() failed");
        }

        _running.store(true);
        _thread = std::thread([this] { this->loop(); });
    }

    ~TinyHttpServer() {
        _running.store(false);
        tryWake();

        if (_thread.joinable())
            _thread.join();
        if (socket_valid(_listenSock))
            closesock(_listenSock);
    }

    int port() const {
        return _port;
    }

    std::optional<std::string> lastPostBody() const {
        std::lock_guard<std::mutex> g(_mtx);
        return _lastPostBody;
    }

  private:
    void tryWake() {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (!socket_valid(s))
            return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(_port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(s, (sockaddr*)&addr, sizeof(addr));
        closesock(s);
    }

    void loop() {
        while (_running.load()) {
            sockaddr_in client{};
            socklen_t clen = sizeof(client);
            SOCKET c = ::accept(_listenSock, (sockaddr*)&client, &clen);
            if (!socket_valid(c))
                continue;

            std::string raw;
            raw.reserve(4096);
            char buf[2048];

            while (true) {
#ifdef _WIN32
                int n = ::recv(c, buf, (int)sizeof(buf), 0);
#else
                int n = ::recv(c, buf, sizeof(buf), 0);
#endif
                if (n <= 0)
                    break;
                raw.append(buf, buf + n);
                if (raw.find("\r\n\r\n") != std::string::npos)
                    break;
            }

            ParsedRequest req = parseHttpRequest(raw);
            auto itCL = req.headers.find("content-length");
            size_t wantBody = 0;
            if (itCL != req.headers.end()) {
                wantBody = static_cast<size_t>(std::stoul(itCL->second));
            }

            auto headerEnd = raw.find("\r\n\r\n");
            std::string alreadyBody;
            if (headerEnd != std::string::npos) {
                alreadyBody = raw.substr(headerEnd + 4);
            }
            while (alreadyBody.size() < wantBody) {
#ifdef _WIN32
                int n = ::recv(c, buf, (int)sizeof(buf), 0);
#else
                int n = ::recv(c, buf, sizeof(buf), 0);
#endif
                if (n <= 0)
                    break;
                alreadyBody.append(buf, buf + n);
            }
            if (wantBody > 0)
                req.body = alreadyBody.substr(0, wantBody);

            const std::string etag = R"(W/"abc")";

            if (req.method == "GET" && req.path == "/etag") {
                auto inm = req.headers.find("if-none-match");
                if (inm != req.headers.end() && inm->second == etag) {
                    auto resp = httpResponse(304, {{"ETag", etag}}, "");
                    ::send(c, resp.c_str(), (int)resp.size(), 0);
                } else {
                    auto resp =
                        httpResponse(200, {{"Content-Type", "application/json"}, {"ETag", etag}}, R"({"ok":true})");
                    ::send(c, resp.c_str(), (int)resp.size(), 0);
                }
            } else if (req.method == "POST" && req.path == "/post") {
                {
                    std::lock_guard<std::mutex> g(_mtx);
                    _lastPostBody = req.body;
                }
                auto resp = httpResponse(200, {{"Content-Type", "text/plain"}}, "ok");
                ::send(c, resp.c_str(), (int)resp.size(), 0);
            } else {
                auto resp = httpResponse(404, {{"Content-Type", "text/plain"}}, "not found");
                ::send(c, resp.c_str(), (int)resp.size(), 0);
            }

            closesock(c);
        }
    }

  private:
    WinsockRAII _wsa;
    SOCKET _listenSock{};
    int _port{0};
    std::atomic<bool> _running{false};
    std::thread _thread;

    mutable std::mutex _mtx;
    std::optional<std::string> _lastPostBody;
};

struct DummyRequest : public IComRequest {
    std::string type() const override {
        return "dummy";
    }
};

} // namespace

TEST(HttpClient, ReturnsTypeMismatchForNonHttpRequest) {
    unleash::HttpClient client;

    DummyRequest req;
    auto resp = client.request(req);

    ASSERT_NE(resp, nullptr);

    auto* err = dynamic_cast<ErrorResponse*>(resp.get());
    ASSERT_NE(err, nullptr);
    EXPECT_EQ(err->code(), ComErrorEnum::TypeMismatch);
    EXPECT_EQ(resp->status, -1);
}

TEST(HttpClient, Get200ParsesBodyStatusAndHeadersIncludingETag) {
    TinyHttpServer server;

    unleash::HttpClient client;
    unleash::HttpRequest req;
    req.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/etag";
    req.usePOSTrequests = false;
    req.timeoutMs = 3000;
    req.headers["Accept"] = "application/json";

    auto respBase = client.request(req);
    ASSERT_NE(respBase, nullptr);

    auto* resp = dynamic_cast<unleash::HttpResponse*>(respBase.get());
    ASSERT_NE(resp, nullptr);

    EXPECT_EQ(resp->status, 200);
    EXPECT_EQ(resp->body, R"({"ok":true})");

    // Header keys are lowercased by your headerCb
    auto it = resp->headers.find("etag");
    ASSERT_NE(it, resp->headers.end());
    EXPECT_EQ(it->second, R"(W/"abc")");
}

TEST(HttpClient, Get304WhenIfNoneMatchMatchesETag) {
    TinyHttpServer server;

    unleash::HttpClient client;

    unleash::HttpRequest req1;
    req1.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/etag";
    req1.timeoutMs = 3000;

    auto r1 = client.request(req1);
    auto* resp1 = dynamic_cast<unleash::HttpResponse*>(r1.get());
    ASSERT_NE(resp1, nullptr);
    ASSERT_EQ(resp1->status, 200);

    auto it = resp1->headers.find("etag");
    ASSERT_NE(it, resp1->headers.end());
    const std::string etag = it->second;

    unleash::HttpRequest req2;
    req2.url = req1.url;
    req2.timeoutMs = 3000;
    req2.headers["If-None-Match"] = etag;

    auto r2 = client.request(req2);
    auto* resp2 = dynamic_cast<unleash::HttpResponse*>(r2.get());
    ASSERT_NE(resp2, nullptr);

    EXPECT_EQ(resp2->status, 304);
    EXPECT_TRUE(resp2->body.empty());
}

TEST(HttpClient, PostSendsBodyAndServerReceivesIt) {
    TinyHttpServer server;

    unleash::HttpClient client;

    unleash::HttpRequest req;
    req.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/post";
    req.usePOSTrequests = true;
    req.timeoutMs = 3000;
    req.headers["Content-Type"] = "application/json";
    req.body = R"({"context":{"appName":"a","environment":"dev","sessionId":"s"}})";

    auto respBase = client.request(req);
    auto* resp = dynamic_cast<unleash::HttpResponse*>(respBase.get());
    ASSERT_NE(resp, nullptr);

    EXPECT_EQ(resp->status, 200);
    EXPECT_EQ(resp->body, "ok");

    // Verify server received the exact body
    auto got = server.lastPostBody();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, req.body);
}
