#include "unleash/Configuration/clientConfig.hpp"
#include "unleash/Utils/utils.hpp"

namespace unleash {

Bootstrap::Bootstrap(ToggleSet::Map p_map)
    : _map(std::move(p_map))

{}
const ToggleSet::Map& Bootstrap::getToggles() const {
    return _map;
}

ClientConfig::ClientConfig(const std::string& p_url, const std::string& p_clientKey, const std::string& p_appName)
    : _url(p_url), _clientKey(p_clientKey), _appName(p_appName) {
    if (_appName.empty()) {
        // define a logging strategy here!
        _appName = std::string(utils::defaultAppName);
    }
    _connectionId = utils::uuidv4Generator();
    _storageProvider = std::make_shared<LocalStorageProvider>();
}

ClientConfig& ClientConfig::setInstanceId(const std::string& p_instanceId) {
    _instanceId = p_instanceId;
    return *this;
}

ClientConfig& ClientConfig::setRefreshInterval(utils::seconds s) {
    _refreshInterval = s;
    return *this;
}

ClientConfig& ClientConfig::setMetricsInterval(utils::seconds s) {
    _metricsInterval = s;
    return *this;
}

ClientConfig& ClientConfig::setMetricsIntervalInitial(utils::seconds s) {
    _metricsIntervalInitial = s;
    return *this;
}

ClientConfig& ClientConfig::setBootstrap(Bootstrap b) {
    _bootstrap = std::move(b);
    return *this;
}

ClientConfig& ClientConfig::setBootstrapOverride(bool v) {
    _bootstrapOverride = v;
    return *this;
}

ClientConfig& ClientConfig::setHeaderName(std::string p_headerName) {
    _headerName = std::move(p_headerName);
    return *this;
}

ClientConfig& ClientConfig::setCustomHeaders(std::map<std::string, std::string> p_headers) {
    _customHeaders = std::move(p_headers);
    return *this;
}

ClientConfig& ClientConfig::setImpressionDataAll(bool v) {
    _impressionDataAll = v;
    return *this;
}

ClientConfig& ClientConfig::setUsePostRequests(bool v) {
    _usePostRequests = v;
    return *this;
}

ClientConfig& ClientConfig::setTimeOutQueryMS(utils::mSeconds m) {
    _timeOutQueryMS = m;
    return *this;
}

ClientConfig& ClientConfig::setStorageProvider(std::shared_ptr<IStorageProvider> provider) {
    if (provider) {
        _storageProvider = std::move(provider);
    }
    return *this;
}

const std::string& ClientConfig::url() const {
    return _url;
}

const std::string& ClientConfig::clientKey() const {
    return _clientKey;
}

const std::string& ClientConfig::appName() const {
    return _appName;
}

const std::string& ClientConfig::connectionId() const {
    return _connectionId;
}

const std::string& ClientConfig::instanceId() const {
    return _instanceId;
}

utils::seconds ClientConfig::refreshInterval() const {
    return _refreshInterval;
}

utils::seconds ClientConfig::metricsInterval() const {
    return _metricsInterval;
}

utils::seconds ClientConfig::metricsIntervalInitial() const {
    return _metricsIntervalInitial;
}

const std::optional<Bootstrap>& ClientConfig::bootstrap() const {
    return _bootstrap;
}

bool ClientConfig::bootstrapOverride() const {
    return _bootstrapOverride;
}

const std::string& ClientConfig::headerName() const {
    return _headerName;
}

const std::map<std::string, std::string>& ClientConfig::customHeaders() const {
    return _customHeaders;
}

bool ClientConfig::impressionDataAll() const {
    return _impressionDataAll;
}

bool ClientConfig::usePostRequests() const {
    return _usePostRequests;
}

utils::mSeconds ClientConfig::timeOutQueryMS() const {
    return _timeOutQueryMS;
}

bool ClientConfig::isRefreshEnabled() const {
    return (_refreshInterval.count() > 0);
}
bool ClientConfig::isMetricsEnabled() const {
    return (_metricsInterval.count() > 0);
}

std::shared_ptr<IStorageProvider> ClientConfig::storageProvider() const {
    return _storageProvider;
}

bool ClientConfig::isValid() {
    if (_url.empty() || _clientKey.empty() || _refreshInterval.count() < 0 || _metricsInterval.count() < 0 ||
        _metricsIntervalInitial.count() < 0) {
        // define a logging strategy here!
        return false;
    }
    return true;
}

} // namespace unleash