#pragma once
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <chrono>
#include <vector>
#include <string_view>
#include "unleash/Utils/utils.hpp"

namespace unleash {

inline bool isReservedContextKey(std::string_view key) {
    return key == "appName" || key == "environment" || key == "userId" || key == "sessionId" ||
           key == "remoteAddress" || key == "currentTime";
}

class MutableContext final {
  public:
    MutableContext() = default;
    MutableContext& setUserId(const std::string& p_userId);
    MutableContext& setRemoteAddress(const std::string& p_remoteAddress);
    MutableContext& setCurrentTime();
    MutableContext& setProperty(std::string key, std::string value);

    const std::optional<std::string>& getUserId() const;
    const std::optional<std::string>& getRemoteAddress() const;
    const std::optional<std::string>& getCurrentTime() const;
    const utils::contextProperties& getProperties() const;

  private:
    std::optional<std::string> _userId;
    std::optional<std::string> _remoteAddress;
    std::optional<std::string> _currentTime;
    utils::contextProperties _properties;
};

class Context final {
  public:
    // properties appName, environnement, sessionId are mendatory to set, if p_sessionid is empty resolveSessionId()
    // will generate a random sessionId following the same algorithm used in th JS equivalent.
    Context(const std::string& p_appName = std::string(utils::defaultAppName),
            const std::string& p_environment = std::string(), const std::string& p_sessionId = std::string());

    const std::string& getAppName() const;
    const std::string& getSessionId() const;
    const std::optional<std::string> getEnvironment() const;

    const std::optional<std::string> getUserId() const;
    const std::optional<std::string> getRemoteAddress() const;
    const std::optional<std::string> getCurrentTime() const;
    const utils::contextProperties getProperties() const;

    Context& setUserId(const std::string& p_userId);
    Context& setRemoteAddress(const std::string& p_remoteAddress);
    Context& setCurrentTime();
    Context& setProperty(std::string key, std::string value);

    void updateMutableContext(const MutableContext& p_mutableContext);

  private:
    void resolveSessionId();
    std::string _appName;
    std::string _sessionId;
    std::optional<std::string> _environment;
    MutableContext _mutableContext;
};

} // namespace unleash