#include <gtest/gtest.h>

#include "unleash/Fetcher/toggleFetcher.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <iostream>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;

static inline int socket_last_error() {
    return WSAGetLastError();
}
static inline int socket_shutdown(socket_t s) {
    return ::shutdown(s, SD_BOTH);
}
static inline int socket_close(socket_t s) {
    return ::closesocket(s);
}

struct WsaGuard {
    WsaGuard() {
        WSADATA d{};
        const int rc = ::WSAStartup(MAKEWORD(2, 2), &d);
        if (rc != 0)
            throw std::runtime_error("WSAStartup() failed: " + std::to_string(rc));
    }
    ~WsaGuard() {
        ::WSACleanup();
    }
};

#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;

static inline int socket_last_error() {
    return errno;
}
static inline int socket_shutdown(socket_t s) {
    return ::shutdown(s, SHUT_RDWR);
}
static inline int socket_close(socket_t s) {
    return ::close(s);
}
#endif
// ----------------------------------------------------------------

namespace {

static std::string kSampleFeaturesJson() {
    return R"JSON(
{
  "version": 1,
  "toggles": [
    {
      "name": "flagA",
      "description": "test flag",
      "enabled": true,
      "strategies": [],
      "variants": []
    }
  ]
}
)JSON";
}

static std::optional<std::string> getHeaderValueLowerKey(const std::string& request, const std::string& lowerKey) {
    auto toLower = [](std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    std::istringstream iss(request);
    std::string line;

    std::getline(iss, line);

    while (std::getline(iss, line)) {
        if (line == "\r" || line.empty())
            break;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
            val.erase(val.begin());

        if (toLower(key) == lowerKey)
            return val;
    }
    return std::nullopt;
}

static void setSocketTimeouts(socket_t s, int milliseconds) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(milliseconds);
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static void sendRaw(socket_t fd, const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
#ifdef _WIN32
        const int n = ::send(fd, p, static_cast<int>(left), 0);
#else
        const ssize_t n = ::send(fd, p, left, 0);
#endif
        if (n <= 0)
            return;
        p += static_cast<size_t>(n);
        left -= static_cast<size_t>(n);
    }
}

static std::optional<size_t> parseContentLengthFromHeadersOnly(const std::string& headersOnly) {
    auto v = getHeaderValueLowerKey(headersOnly, "content-length");
    if (!v)
        return std::nullopt;
    try {
        return static_cast<size_t>(std::stoull(*v));
    } catch (...) {
        return std::nullopt;
    }
}

class MiniHttpServer {
  public:
    struct Observations {
        std::atomic<int> requests{0};
        std::atomic<bool> sawIfNoneMatchOnSecond{false};
        std::string lastRequestLine;
    };

    MiniHttpServer() {
#ifdef _WIN32
        _wsa.emplace();
#endif
        _listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_listenFd == kInvalidSocket)
            throw std::runtime_error("socket() failed: " + std::to_string(socket_last_error()));

        int yes = 1;
#ifdef _WIN32
        ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
        ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0); // ephemeral port

        if (::bind(_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error("bind() failed: " + std::to_string(socket_last_error()));

        socklen_t len = sizeof(addr);
        if (::getsockname(_listenFd, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
            throw std::runtime_error("getsockname() failed: " + std::to_string(socket_last_error()));

        _port = ntohs(addr.sin_port);

        if (::listen(_listenFd, 16) < 0)
            throw std::runtime_error("listen() failed: " + std::to_string(socket_last_error()));

        _thread = std::thread([this] { loop(); });
    }

    ~MiniHttpServer() {
        _stop.store(true);

        if (_listenFd != kInvalidSocket) {
            socket_close(_listenFd);
            _listenFd = kInvalidSocket;
        }

        if (_thread.joinable())
            _thread.join();
    }

    int port() const {
        return _port;
    }
    Observations& obs() {
        return _obs;
    }
    std::string lastRequestLine() const {
        std::lock_guard<std::mutex> lk(_obsMutex);
        return _obs.lastRequestLine;
    }

    void setForce500OnNext(bool v) {
        _force500Next.store(v);
    }

  private:
    // -----------------------------------------------------------------------
    void loop() {
        while (!_stop.load()) {

            const socket_t listenFd = _listenFd;
            if (listenFd == kInvalidSocket)
                break;

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listenFd, &rfds);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100'000;

#ifdef _WIN32
            // First argument is ignored on Windows.
            const int ret = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
            const int ret = ::select(listenFd + 1, &rfds, nullptr, nullptr, &tv);
#endif
            if (ret <= 0) {
                // ret == 0
                // ret == -1
                continue;
            }

            sockaddr_in client{};
            socklen_t clen = sizeof(client);
            socket_t cfd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&client), &clen);
            if (cfd == kInvalidSocket) {
                if (_stop.load())
                    break;
                continue;
            }

            setSocketTimeouts(cfd, 3000);

            std::string req = readRequest(cfd);
            const int idx = ++_obs.requests;
            {
                std::lock_guard<std::mutex> lk(_obsMutex);
                std::istringstream iss(req);
                std::getline(iss, _obs.lastRequestLine);
                if (!_obs.lastRequestLine.empty() && _obs.lastRequestLine.back() == '\r') {
                    _obs.lastRequestLine.pop_back();
                }
            }

            if (_force500Next.exchange(false)) {
                sendResponse(cfd, 500, "Internal Server Error", "text/plain", "boom", std::nullopt);
                socket_shutdown(cfd);
                socket_close(cfd);
                continue;
            }

            if (idx == 1) {
                sendResponse(cfd, 200, "OK", "application/json", kSampleFeaturesJson(),
                             std::string("W/\"unit-test-etag-1\""));
            } else if (idx == 2) {
                auto inm = getHeaderValueLowerKey(req, "if-none-match");
                if (inm && *inm == "W/\"unit-test-etag-1\"") {
                    _obs.sawIfNoneMatchOnSecond.store(true);
                    sendResponse(cfd, 304, "Not Modified", "text/plain", "", std::string("W/\"unit-test-etag-1\""));
                } else {
                    sendResponse(cfd, 200, "OK", "application/json", kSampleFeaturesJson(),
                                 std::string("W/\"unit-test-etag-1\""));
                }
            } else {
                sendResponse(cfd, 200, "OK", "application/json", kSampleFeaturesJson(),
                             std::string("W/\"unit-test-etag-1\""));
            }

            socket_shutdown(cfd);
            socket_close(cfd);
        }
    }

    static std::string readRequest(socket_t fd) {
        std::string data;
        data.reserve(8192);
        char buf[1024];

        while (data.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
            const int n = ::recv(fd, buf, static_cast<int>(sizeof(buf)), 0);
#else
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
#endif
            if (n <= 0)
                break;
            data.append(buf, buf + n);
            if (data.size() > 1024 * 1024)
                break; // safety cap
        }

        const auto headersEnd = data.find("\r\n\r\n");
        if (headersEnd == std::string::npos)
            return data;

        const std::string headersOnly = data.substr(0, headersEnd + 4);

        if (auto ex = getHeaderValueLowerKey(headersOnly, "expect"); ex)
            sendRaw(fd, "HTTP/1.1 100 Continue\r\n\r\n");

        auto cl = parseContentLengthFromHeadersOnly(headersOnly);
        if (!cl)
            return data;

        const size_t alreadyHave = data.size() - (headersEnd + 4);
        size_t remaining = (*cl > alreadyHave) ? (*cl - alreadyHave) : 0;

        while (remaining > 0) {
            const size_t want = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
#ifdef _WIN32
            const int n = ::recv(fd, buf, static_cast<int>(want), 0);
#else
            const ssize_t n = ::recv(fd, buf, want, 0);
#endif
            if (n <= 0)
                break;
            data.append(buf, buf + n);
            remaining -= static_cast<size_t>(n);
        }

        return data;
    }

    static void sendAll(socket_t fd, const std::string& s) {
        const char* p = s.data();
        size_t left = s.size();
        while (left > 0) {
#ifdef _WIN32
            // Keep chunk within int range (avoid issues with large buffers).
            const int chunk = static_cast<int>((left > 64u * 1024u) ? 64u * 1024u : left);
            const int n = ::send(fd, p, chunk, 0);
#else
            const ssize_t n = ::send(fd, p, left, 0);
#endif
            if (n <= 0)
                return;
            p += static_cast<size_t>(n);
            left -= static_cast<size_t>(n);
        }
    }

    static void sendResponse(socket_t fd, int status, const std::string& statusText, const std::string& contentType,
                             const std::string& body, const std::optional<std::string>& etag) {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << statusText << "\r\n";
        oss << "Connection: close\r\n";
        if (etag)
            oss << "ETag: " << *etag << "\r\n";
        oss << "Content-Type: " << contentType << "\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "\r\n";
        oss << body;

        sendAll(fd, oss.str());
    }

    socket_t _listenFd{kInvalidSocket};
    int _port{0};
    std::thread _thread;
    std::atomic<bool> _stop{false};
    std::atomic<bool> _force500Next{false};
    Observations _obs{};
    mutable std::mutex _obsMutex;

#ifdef _WIN32
    std::optional<WsaGuard> _wsa{};
#endif
};

} // namespace

// ===========================================================================
// Tests
// ===========================================================================

TEST(ToggleFetcher, Fetch200ReturnsTogglesAndCachesEtag_Then304WhenNotModified) {
    MiniHttpServer server;

    const std::string baseUrl = "http://127.0.0.1:" + std::to_string(server.port());
    const std::string baseKey = "dummy-client-key";
    const std::string baseNameApp = "unitApp";

    unleash::ClientConfig cfg(baseUrl, baseKey, baseNameApp);

    unleash::Context ctx("unitApp", "dev", "sess-1");
    ctx.setCurrentTime().setUserId("user-1").setRemoteAddress("127.0.0.1");

    unleash::ToggleFetcher fetcher(cfg);

    // First fetch: expect ===> 304 + toggles
    auto r1 = fetcher.fetch(ctx);
    std::cout << "#######################################################################\n";
    std::cout << "Status is: " << r1.status
              << " and error is: " << (r1.error.has_value() ? r1.error.value() : "<No value>") << "\n";

    EXPECT_EQ(r1.status, 200);
    EXPECT_FALSE(r1.error.has_value());
    ASSERT_TRUE(r1.toggles.has_value());
    EXPECT_EQ(r1.toggles.value().size(), 1u);

    // Second fetch: server expects If-None-Match ===> 304
    auto r2 = fetcher.fetch(ctx);
    EXPECT_EQ(r2.status, 304);
    EXPECT_FALSE(r2.error.has_value());
    EXPECT_FALSE(r2.toggles.has_value());

    EXPECT_TRUE(server.obs().sawIfNoneMatchOnSecond.load());
}

TEST(ToggleFetcher, Fetch500ReturnsError) {
    MiniHttpServer server;

    const std::string baseUrl = "http://127.0.0.1:" + std::to_string(server.port());
    unleash::ClientConfig cfg(baseUrl, "dummy-client-key", "unitApp");

    unleash::Context ctx("unitApp", "dev", "sess-1");
    ctx.setCurrentTime().setUserId("user-1").setRemoteAddress("127.0.0.1");

    unleash::ToggleFetcher fetcher(cfg);

    server.setForce500OnNext(true);

    auto r = fetcher.fetch(ctx);
    EXPECT_EQ(r.status, 500);
    EXPECT_FALSE(r.toggles.has_value());

    if (r.error.has_value()) {
        EXPECT_FALSE(r.error->empty());
    }
}

TEST(ToggleFetcher, GetRequestIncludesContextAsQueryParams) {
    MiniHttpServer server;

    const std::string baseUrl = "http://127.0.0.1:" + std::to_string(server.port());
    unleash::ClientConfig cfg(baseUrl, "dummy-client-key", "unitApp");
    cfg.setUsePostRequests(false);

    unleash::Context ctx("unitApp", "dev", "sess-1");
    ctx.setUserId("user-1")
        .setRemoteAddress("10.0.0.1")
        .setProperty("tenant", "acmé")
        .setProperty("plan", "pro")
        .setProperty("note", "hello world");

    unleash::ToggleFetcher fetcher(cfg);
    auto r = fetcher.fetch(ctx);
    EXPECT_EQ(r.status, 200);

    const std::string line = server.lastRequestLine();
    EXPECT_NE(line.find("GET /?"), std::string::npos);
    EXPECT_NE(line.find("appName=unitApp"), std::string::npos);
    EXPECT_NE(line.find("environment=dev"), std::string::npos);
    EXPECT_NE(line.find("sessionId=sess-1"), std::string::npos);
    EXPECT_NE(line.find("userId=user-1"), std::string::npos);
    EXPECT_NE(line.find("remoteAddress=10.0.0.1"), std::string::npos);
    EXPECT_NE(line.find("properties%5Btenant%5D=acm%C3%A9"), std::string::npos);
    EXPECT_NE(line.find("properties%5Bplan%5D=pro"), std::string::npos);
    EXPECT_NE(line.find("properties%5Bnote%5D=hello%20world"), std::string::npos);
}
