#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdv::ros_bridge {

/// Thread-safe WebSocket server broadcasting UTF-8 JSON text frames to all clients.
class TelemetryWebSocketServer {
public:
  TelemetryWebSocketServer();
  ~TelemetryWebSocketServer();

  TelemetryWebSocketServer(const TelemetryWebSocketServer&) = delete;
  TelemetryWebSocketServer& operator=(const TelemetryWebSocketServer&) = delete;

  bool start(const std::string& host, uint16_t port, std::string* error_out);
  void stop();
  void broadcast(const std::string& json_utf8);
  std::size_t clientCount() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sdv::ros_bridge
