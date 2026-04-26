#include "unleash/Domain/context.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <regex>
#include <ctime>
#include <random>

namespace unleash {

MutableContext& MutableContext::setUserId(const std::string& p_userId) {
    if (p_userId.empty())
        _userId.reset();
    else
        _userId = p_userId;
    return *this;
}

MutableContext& MutableContext::setRemoteAddress(const std::string& p_remoteAddress) {
    if (p_remoteAddress.empty())
        _remoteAddress.reset();
    else
        _remoteAddress = p_remoteAddress;
    return *this;
}

MutableContext& MutableContext::setCurrentTime() {
    _currentTime = utils::getISO8601CurrentTimeStamp();
    return *this;
}

MutableContext& MutableContext::setProperty(std::string key, std::string value) {
    if (key.empty())
        return *this;
    if (isReservedContextKey(key)) {
        // define a logging strategy here!
        return *this;
    }

    auto it = std::find_if(_properties.begin(), _properties.end(), [&](const auto& kv) { return kv.first == key; });
    if (it != _properties.end())
        it->second = std::move(value);
    else
        _properties.emplace_back(std::move(key), std::move(value));
    return *this;
}

const std::optional<std::string>& MutableContext::getUserId() const {
    return _userId;
}

const std::optional<std::string>& MutableContext::getRemoteAddress() const {
    return _remoteAddress;
}

const std::optional<std::string>& MutableContext::getCurrentTime() const {
    return _currentTime;
}

const utils::contextProperties& MutableContext::getProperties() const {
    return _properties;
}

Context::Context(const std::string& p_appName, const std::string& p_environment, const std::string& p_sessionId)
    : _appName(p_appName), _sessionId(p_sessionId) {
    if (_appName.empty()) {
        // define a logging strategy here!
        _appName = std::string(utils::defaultAppName);
    }
    if (_sessionId.empty())
        this->resolveSessionId();
    if (!p_environment.empty())
        _environment = p_environment;
}

const std::string& Context::getAppName() const {
    return _appName;
}

const std::string& Context::getSessionId() const {
    return _sessionId;
}

const std::optional<std::string> Context::getEnvironment() const {
    return _environment;
}

const std::optional<std::string> Context::getUserId() const {
    return _mutableContext.getUserId();
}

const std::optional<std::string> Context::getRemoteAddress() const {
    return _mutableContext.getRemoteAddress();
}

const std::optional<std::string> Context::getCurrentTime() const {
    return _mutableContext.getCurrentTime();
}

const utils::contextProperties Context::getProperties() const {
    return _mutableContext.getProperties();
}

Context& Context::setUserId(const std::string& p_userId) {
    _mutableContext.setUserId(p_userId);
    return *this;
}

Context& Context::setRemoteAddress(const std::string& p_remoteAddress) {
    _mutableContext.setRemoteAddress(p_remoteAddress);
    return *this;
}

Context& Context::setCurrentTime() {
    _mutableContext.setCurrentTime();
    return *this;
}

Context& Context::setProperty(std::string key, std::string value) {
    _mutableContext.setProperty(key, value);
    return *this;
}

void Context::updateMutableContext(const MutableContext& p_mutableContext) {
    _mutableContext = p_mutableContext;
}

void Context::resolveSessionId() {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_int_distribution<unsigned int> dist(1, 1000000000);

    unsigned int randomNum = dist(gen);
    _sessionId = std::to_string(randomNum);
}

} // namespace unleash