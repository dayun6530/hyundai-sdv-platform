#include <gtest/gtest.h>

#include <optional>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "unleash/Metrics/metricSender.hpp"
#include "unleash/Configuration/clientConfig.hpp"

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

static inline bool socket_valid(socket_t s) {
    return s != INVALID_SOCKET;
}
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

static inline bool socket_valid(socket_t s) {
    return s >= 0;
}
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

using namespace unleash;

namespace {

struct CapturedRequest {
    std::string requestLine;
    std::map<std::string, std::string> headersLower;
    std::string body;
};

static std::string toLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static void trim(std::string& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
}

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

static void sendAll(socket_t fd, const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
#ifdef _WIN32
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

struct HeadRead {
    std::string headers;
    std::string remainder;
};

static HeadRead recvHeadersWithRemainder(socket_t fd) {
    std::string buf;
    buf.reserve(4096);

    char tmp[1024];
    while (true) {
        auto pos = buf.find("\r\n\r\n");
        if (pos != std::string::npos) {
            HeadRead out;
            const std::size_t end = pos + 4;
            out.headers = buf.substr(0, end);
            out.remainder = buf.substr(end);
            return out;
        }
#ifdef _WIN32
        const int n = ::recv(fd, tmp, static_cast<int>(sizeof(tmp)), 0);
#else
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
#endif
        if (n <= 0) {
            HeadRead out;
            out.headers = buf;
            return out;
        }
        buf.append(tmp, tmp + n);

        if (buf.size() > 1024 * 1024) {
            HeadRead out;
            out.headers = buf;
            return out;
        }
    }
}

static std::string readBodyContentLength(socket_t fd, std::string remainder, std::size_t contentLen) {
    std::string body;
    body.reserve(contentLen);

    if (!remainder.empty()) {
        const std::size_t take = (contentLen < remainder.size()) ? contentLen : remainder.size();
        body.append(remainder.data(), take);
        remainder.erase(0, take);
    }

    char tmp[4096];
    while (body.size() < contentLen) {
        const std::size_t need = contentLen - body.size();
        const std::size_t ask = (need < sizeof(tmp)) ? need : sizeof(tmp);
#ifdef _WIN32
        const int n = ::recv(fd, tmp, static_cast<int>(ask), 0);
#else
        const ssize_t n = ::recv(fd, tmp, ask, 0);
#endif
        if (n <= 0)
            break;
        body.append(tmp, tmp + n);
    }

    return body;
}

static std::string recvChunkedBody(socket_t fd, std::string remainder) {
    std::string body;
    std::string buf = std::move(remainder);

    auto recvSome = [&](std::string& dst) -> bool {
        char tmp[4096];
#ifdef _WIN32
        const int n = ::recv(fd, tmp, static_cast<int>(sizeof(tmp)), 0);
#else
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
#endif
        if (n <= 0)
            return false;
        dst.append(tmp, tmp + n);
        return true;
    };

    auto readLine = [&](std::string& line) -> bool {
        while (true) {
            auto pos = buf.find("\r\n");
            if (pos != std::string::npos) {
                line = buf.substr(0, pos);
                buf.erase(0, pos + 2);
                return true;
            }
            if (!recvSome(buf))
                return false;
        }
    };

    while (true) {
        std::string line;
        if (!readLine(line))
            return body;

        auto semi = line.find(';');
        if (semi != std::string::npos)
            line = line.substr(0, semi);

        std::size_t chunkSize = 0;
        try {
            chunkSize = std::stoul(line, nullptr, 16);
        } catch (...) {
            return body;
        }

        if (chunkSize == 0) {
            std::string trailer;
            (void)readLine(trailer);
            return body;
        }

        while (buf.size() < chunkSize + 2) {
            if (!recvSome(buf))
                break;
        }
        if (buf.size() < chunkSize + 2) {
            const std::size_t take = (chunkSize < buf.size()) ? chunkSize : buf.size();
            body.append(buf.data(), take);
            return body;
        }

        body.append(buf.data(), chunkSize);
        buf.erase(0, chunkSize + 2);
    }
}

static CapturedRequest parseHeadersOnly(const std::string& head, std::int64_t& contentLen, bool& isChunked,
                                        bool& expect100Continue) {
    CapturedRequest cr;

    std::istringstream iss(head);
    std::string line;

    std::getline(iss, line);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    cr.requestLine = line;

    contentLen = 0;
    isChunked = false;
    expect100Continue = false;

    while (std::getline(iss, line)) {
        if (line == "\r" || line.empty())
            break;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;

        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);
        trim(k);
        trim(v);

        const auto kl = toLower(k);
        const auto vl = toLower(v);

        cr.headersLower[kl] = v;

        if (kl == "content-length") {
            try {
                contentLen = std::stoll(v);
            } catch (...) {
                contentLen = 0;
            }
        } else if (kl == "transfer-encoding" && contains(vl, "chunked")) {
            isChunked = true;
        } else if (kl == "expect" && contains(vl, "100-continue")) {
            expect100Continue = true;
        }
    }

    return cr;
}

class MiniHttpServer {
  public:
    explicit MiniHttpServer(int responseStatus) : _responseStatus(responseStatus) {
#ifdef _WIN32
        _wsa.emplace();
#endif
        _listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (!socket_valid(_listenFd))
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
        addr.sin_port = htons(0);

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

        if (socket_valid(_listenFd)) {
            socket_close(_listenFd);
            _listenFd = kInvalidSocket;
        }

        if (_thread.joinable())
            _thread.join();
    }

    int port() const {
        return _port;
    }

    bool waitForOneRequest(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (_gotRequest.load())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return _gotRequest.load();
    }

    CapturedRequest captured() const {
        return _captured;
    }

  private:
    void loop() {
        while (!_stop.load()) {

            const socket_t listenFd = _listenFd;
            if (!socket_valid(listenFd))
                break;

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listenFd, &rfds);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100'000;

#ifdef _WIN32
            const int ret = ::select(0, &rfds, nullptr, nullptr, &tv);
#else
            const int ret = ::select(listenFd + 1, &rfds, nullptr, nullptr, &tv);
#endif
            if (ret <= 0)
                continue;

            sockaddr_in client{};
            socklen_t clen = sizeof(client);
            socket_t cfd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&client), &clen);
            if (!socket_valid(cfd)) {
                if (_stop.load())
                    break;
                continue;
            }

#ifdef _WIN32
            DWORD tvMs = 2000;
            ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
#else
            timeval tvSock{};
            tvSock.tv_sec = 2;
            ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tvSock, sizeof(tvSock));
#endif

            auto hr = recvHeadersWithRemainder(cfd);
            std::string remainder = hr.remainder;

            std::int64_t contentLen = 0;
            bool isChunked = false;
            bool expect100 = false;

            _captured = parseHeadersOnly(hr.headers, contentLen, isChunked, expect100);

            if (expect100)
                sendAll(cfd, "HTTP/1.1 100 Continue\r\n\r\n");

            // 3) Read body.
            if (isChunked) {
                _captured.body = recvChunkedBody(cfd, std::move(remainder));
            } else if (contentLen > 0) {
                _captured.body = readBodyContentLength(cfd, std::move(remainder), static_cast<std::size_t>(contentLen));
            } else {
                _captured.body.clear();
            }

            // 4) Send response.
            const std::string statusText = (_responseStatus == 200)   ? "OK"
                                           : (_responseStatus == 500) ? "Internal Server Error"
                                                                      : "OK";

            std::ostringstream oss;
            oss << "HTTP/1.1 " << _responseStatus << " " << statusText << "\r\n";
            oss << "Connection: close\r\n";
            oss << "Content-Length: 0\r\n";
            oss << "\r\n";
            sendAll(cfd, oss.str());

            socket_shutdown(cfd);
            socket_close(cfd);

            _gotRequest.store(true);
            return;
        }
    }

    socket_t _listenFd{kInvalidSocket};
    int _port{0};
    int _responseStatus{200};
    std::thread _thread;
    std::atomic<bool> _stop{false};
    std::atomic<bool> _gotRequest{false};
    CapturedRequest _captured{};

#ifdef _WIN32
    std::optional<WsaGuard> _wsa{};
#endif
};

static ClientConfig makeFrontendCfg(const std::string& baseUrlApiFrontend, const std::string& clientKey,
                                    const std::string& appName) {
    ClientConfig cfg(baseUrlApiFrontend, clientKey, appName);
    cfg.setTimeOutQueryMS(std::chrono::milliseconds(1200));
    cfg.setHeaderName("authorization");
    return cfg;
}

} // namespace

// ===========================================================================
// Tests
// ===========================================================================

TEST(MetricSenderTest, SendMetrics_SendsPostWithExpectedPathHeadersAndBody_On200) {
    MiniHttpServer server(200);

    const std::string cfgUrl = "http://127.0.0.1:" + std::to_string(server.port()) + "/api/frontend";

    auto cfg = makeFrontendCfg(cfgUrl, "my-client-key", "unleash-demo2");
    MetricSender sender(cfg);

    const std::string body = R"({"bucket":{"start":"x","stop":"y","toggles":{}},"appName":"a","instanceId":"i"})";
    auto res = sender.sendMetrics(body);

    EXPECT_EQ(res.status, 200);
    EXPECT_FALSE(res.error.has_value());

    ASSERT_TRUE(server.waitForOneRequest(std::chrono::milliseconds(2000)));
    const auto req = server.captured();

    ASSERT_FALSE(req.requestLine.empty());

    {
        std::istringstream iss(req.requestLine);
        std::string method, path, ver;
        iss >> method >> path >> ver;
        EXPECT_EQ(method, "POST");
        EXPECT_EQ(path, "/api/frontend/client/metrics");
    }

    EXPECT_EQ(req.body, body);

    ASSERT_TRUE(req.headersLower.count("accept"));
    EXPECT_EQ(req.headersLower.at("accept"), "application/json");

    ASSERT_TRUE(req.headersLower.count("content-type"));
    EXPECT_EQ(req.headersLower.at("content-type"), "application/json");

    ASSERT_TRUE(req.headersLower.count("unleash-appname"));
    EXPECT_EQ(req.headersLower.at("unleash-appname"), "unleash-demo2");

    ASSERT_TRUE(req.headersLower.count("authorization"));
    EXPECT_EQ(req.headersLower.at("authorization"), "my-client-key");

    ASSERT_TRUE(req.headersLower.count("unleash-connection-id"));
    EXPECT_FALSE(req.headersLower.at("unleash-connection-id").empty());

    ASSERT_TRUE(req.headersLower.count("unleash-sdk"));
    ASSERT_TRUE(req.headersLower.count("user-agent"));
}

TEST(MetricSenderTest, SendMetrics_On500SetsStatusAndError) {
    MiniHttpServer server(500);

    const std::string cfgUrl = "http://127.0.0.1:" + std::to_string(server.port()) + "/api/frontend";

    auto cfg = makeFrontendCfg(cfgUrl, "key", "app");
    MetricSender sender(cfg);

    auto res = sender.sendMetrics(R"({"x":1})");

    EXPECT_EQ(res.status, 500);
    ASSERT_TRUE(res.error.has_value());
    EXPECT_TRUE(contains(*res.error, "HTTP error status=500"));
}

TEST(MetricSenderTest, SendMetrics_WhenConnectionRefusedReturnsError) {
    auto cfg = makeFrontendCfg("http://127.0.0.1:1/api/frontend", "key", "app");
    MetricSender sender(cfg);

    auto res = sender.sendMetrics(R"({"x":1})");
    ASSERT_TRUE(res.error.has_value());
}