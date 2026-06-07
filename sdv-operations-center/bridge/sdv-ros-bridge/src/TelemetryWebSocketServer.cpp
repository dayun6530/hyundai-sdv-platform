#include "sdv/ros_bridge/TelemetryWebSocketServer.hpp"

#include <functional>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace sdv::ros_bridge {

namespace {

class WsSession : public std::enable_shared_from_this<WsSession> {
public:
  WsSession(tcp::socket socket, std::function<void(std::shared_ptr<WsSession>)> on_closed)
  : ws_(std::move(socket)), on_closed_(std::move(on_closed))
  {
  }

  void start()
  {
    ws_.set_option(beast::websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(beast::websocket::stream_base::decorator(
      [](beast::websocket::response_type& res) {
        res.set(beast::http::field::server, "sdv-ros-bridge/1.0");
      }));

    ws_.async_accept(
      beast::bind_front_handler(&WsSession::onAccept, shared_from_this()));
  }

  void send(std::string payload)
  {
    net::dispatch(
      ws_.get_executor(),
      beast::bind_front_handler(&WsSession::enqueueWrite, shared_from_this(), std::move(payload)));
  }

  void close()
  {
    net::dispatch(ws_.get_executor(), [self = shared_from_this()]() {
      beast::error_code ec;
      self->ws_.close(beast::websocket::close_code::normal, ec);
    });
  }

private:
  void onAccept(beast::error_code ec)
  {
    if (ec) {
      notifyClosed();
      return;
    }

    static const std::string welcome = R"({"schema_version":1,"type":"bridge_hello","data":{"service":"sdv-ros-bridge"}})";
    enqueueWrite(welcome);
    doRead();
  }

  void doRead()
  {
    ws_.async_read(
      buffer_,
      beast::bind_front_handler(&WsSession::onRead, shared_from_this()));
  }

  void onRead(beast::error_code ec, std::size_t)
  {
    if (ec == beast::websocket::error::closed) {
      notifyClosed();
      return;
    }
    if (ec) {
      notifyClosed();
      return;
    }

    buffer_.consume(buffer_.size());
    doRead();
  }

  void enqueueWrite(std::string payload)
  {
    write_queue_.push_back(std::move(payload));
    if (!write_in_progress_) {
      doWrite();
    }
  }

  void doWrite()
  {
    if (write_queue_.empty()) {
      write_in_progress_ = false;
      return;
    }

    write_in_progress_ = true;
    ws_.text(true);
    ws_.async_write(
      net::buffer(write_queue_.front()),
      beast::bind_front_handler(&WsSession::onWrite, shared_from_this()));
  }

  void onWrite(beast::error_code ec, std::size_t)
  {
    if (ec) {
      notifyClosed();
      return;
    }

    write_queue_.pop_front();
    if (write_queue_.empty()) {
      write_in_progress_ = false;
      return;
    }
    doWrite();
  }

  void notifyClosed()
  {
    if (on_closed_) {
      on_closed_(shared_from_this());
    }
  }

  beast::websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;
  std::deque<std::string> write_queue_;
  bool write_in_progress_{false};
  std::function<void(std::shared_ptr<WsSession>)> on_closed_;
};

class Listener : public std::enable_shared_from_this<Listener> {
public:
  Listener(
    net::io_context& ioc, tcp::endpoint endpoint,
    std::function<void(std::shared_ptr<WsSession>)> on_session,
    std::function<void(std::shared_ptr<WsSession>)> on_session_closed)
  : ioc_(ioc),
    acceptor_(ioc),
    endpoint_(endpoint),
    on_session_(std::move(on_session)),
    on_session_closed_(std::move(on_session_closed))
  {
    beast::error_code ec;
    acceptor_.open(endpoint_.protocol(), ec);
    if (ec) {
      throw std::runtime_error("acceptor.open: " + ec.message());
    }
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      throw std::runtime_error("acceptor.set_option: " + ec.message());
    }
    acceptor_.bind(endpoint_, ec);
    if (ec) {
      throw std::runtime_error("acceptor.bind: " + ec.message());
    }
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      throw std::runtime_error("acceptor.listen: " + ec.message());
    }
  }

  void run() { doAccept(); }

  void stop()
  {
    beast::error_code ec;
    acceptor_.close(ec);
  }

private:
  void doAccept()
  {
    acceptor_.async_accept(
      net::make_strand(ioc_),
      beast::bind_front_handler(&Listener::onAccept, shared_from_this()));
  }

  void onAccept(beast::error_code ec, tcp::socket socket)
  {
    if (!ec) {
      auto session = std::make_shared<WsSession>(
        std::move(socket),
        [this](std::shared_ptr<WsSession> closed) {
          if (on_session_closed_) {
            on_session_closed_(std::move(closed));
          }
        });
      if (on_session_) {
        on_session_(session);
      }
      session->start();
    }

    if (acceptor_.is_open()) {
      doAccept();
    }
  }

  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  tcp::endpoint endpoint_;
  std::function<void(std::shared_ptr<WsSession>)> on_session_;
  std::function<void(std::shared_ptr<WsSession>)> on_session_closed_;
};

}  // namespace

class TelemetryWebSocketServer::Impl {
public:
  bool start(const std::string& host, const uint16_t port, std::string* error_out)
  {
    if (running_.exchange(true)) {
      if (error_out) {
        *error_out = "already running";
      }
      return false;
    }

    try {
      ioc_ = std::make_unique<net::io_context>(1);
      const auto address = net::ip::make_address(host);
      const tcp::endpoint endpoint{address, port};

      listener_ = std::make_shared<Listener>(
        *ioc_, endpoint,
        [this](std::shared_ptr<WsSession> session) { registerSession(std::move(session)); },
        [this](std::shared_ptr<WsSession> session) { unregisterSession(std::move(session)); });

      listener_->run();
      worker_ = std::thread([this]() {
        ioc_->run();
      });
      return true;
    } catch (const std::exception& ex) {
      running_ = false;
      if (error_out) {
        *error_out = ex.what();
      }
      return false;
    }
  }

  void stop()
  {
    if (!running_.exchange(false)) {
      return;
    }

    if (listener_) {
      listener_->stop();
      listener_.reset();
    }

    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      for (auto& session : sessions_) {
        if (session) {
          session->close();
        }
      }
      sessions_.clear();
    }

    if (ioc_) {
      ioc_->stop();
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    ioc_.reset();
  }

  void broadcast(const std::string& json_utf8)
  {
    std::vector<std::shared_ptr<WsSession>> snapshot;
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      snapshot.reserve(sessions_.size());
      for (auto& session : sessions_) {
        snapshot.push_back(session);
      }
    }

    for (auto& session : snapshot) {
      if (session) {
        session->send(json_utf8);
      }
    }
  }

  std::size_t clientCount() const
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
  }

private:
  void registerSession(std::shared_ptr<WsSession> session)
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.insert(std::move(session));
  }

  void unregisterSession(std::shared_ptr<WsSession> session)
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(std::move(session));
  }

  std::atomic<bool> running_{false};
  std::unique_ptr<net::io_context> ioc_;
  std::shared_ptr<Listener> listener_;
  std::thread worker_;
  mutable std::mutex sessions_mutex_;
  std::set<std::shared_ptr<WsSession>> sessions_;
};

TelemetryWebSocketServer::TelemetryWebSocketServer() : impl_(std::make_unique<Impl>()) {}

TelemetryWebSocketServer::~TelemetryWebSocketServer()
{
  stop();
}

bool TelemetryWebSocketServer::start(
  const std::string& host, const uint16_t port, std::string* error_out)
{
  return impl_->start(host, port, error_out);
}

void TelemetryWebSocketServer::stop()
{
  impl_->stop();
}

void TelemetryWebSocketServer::broadcast(const std::string& json_utf8)
{
  impl_->broadcast(json_utf8);
}

std::size_t TelemetryWebSocketServer::clientCount() const
{
  return impl_->clientCount();
}

}  // namespace sdv::ros_bridge
