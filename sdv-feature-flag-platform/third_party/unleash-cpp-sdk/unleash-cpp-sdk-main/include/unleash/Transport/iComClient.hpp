#pragma once
#include <string>
#include <memory>
#include <atomic>
#include <cstdint>

enum class ComErrorEnum : std::uint8_t { TypeMismatch, VariableMismatch, NetworkError, InitializationError };

struct IComRequest {
    virtual ~IComRequest() = default;
    virtual std::string type() const = 0;
};

struct IComResponse {
    virtual ~IComResponse() = default;
    std::string body;
    long status = 0; // Moved from HttpResponse to be more generic
};

struct ErrorResponse final : public IComResponse {
    ErrorResponse(ComErrorEnum p_code, std::string p_message) : _code(p_code), _message(std::move(p_message)) {
        status = -1; // Indicate error in status
    }

    const std::string& message() const {
        return _message;
    }
    ComErrorEnum code() const {
        return _code;
    }

  private:
    std::string _message;
    ComErrorEnum _code;
};

class IComClient {
  public:
    using CancelToken = std::atomic<bool>;
    virtual ~IComClient() = default;
    virtual std::unique_ptr<IComResponse> request(const IComRequest& req, CancelToken* cancel = nullptr) = 0;
};