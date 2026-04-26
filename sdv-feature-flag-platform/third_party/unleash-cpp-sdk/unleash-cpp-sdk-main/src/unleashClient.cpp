#include "unleash/Client/unleashClient.hpp"
#include "unleash/Utils/utils.hpp"
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace unleash {

UnleashClient::UnleashClient(ClientConfig p_config, Context p_ctx)
    : _config(std::move(p_config)), _context(std::move(p_ctx)), _metricStore(_config), _metricSender(_config),
      _eventHandler(std::make_shared<EventHandler>()), _toggleFetcher(_config) {
    this->initializeToggleCache();
}

UnleashClient::~UnleashClient() {
    stop();
}

void UnleashClient::start() {

    if (_running.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    _eventHandler->emitInit();
    if (isStoreReady()) {
        _eventHandler->emitReady();
        _ready.store(true, std::memory_order_release);
    }
    _exitThreads.store(false, std::memory_order_release);

    _eventHandler->start();

    _mSendingThread = std::thread(&UnleashClient::metricSendingLoop, this);
    _fPollingThread = std::thread(&UnleashClient::featurePollingLoop, this);

    _running.store(true, std::memory_order_release);
}

void UnleashClient::stop() {
    if (!_running.exchange(false, std::memory_order_acq_rel)) {
        return; // not running
    }

    _exitThreads.store(true, std::memory_order_release);

    _cvMetrics.notify_all();
    _cvPolling.notify_all();

    if (_mSendingThread.joinable())
        _mSendingThread.join();
    if (_fPollingThread.joinable())
        _fPollingThread.join();

    _eventHandler->stop();

    _running.store(false, std::memory_order_release);
}

bool UnleashClient::isStoreReady() {
    return _flagStore.isReady();
}

// void UnleashClient::initializeToggleCache()
// {
//     bool hasStoredToggles = false;
//     auto storage = _config.storageProvider();

//     const auto cachedToggles = storage->get();
//     if (cachedToggles.has_value() && cachedToggles->size() > 0) {
//         _flagStore.replace(std::make_shared<ToggleSet>(*cachedToggles));
//         hasStoredToggles = true;
//     }

//     applyBootstrap(hasStoredToggles);
// }

// void UnleashClient::applyBootstrap(bool p_hasStoredToggles)
// {
//     auto bootstrap = _config.bootstrap();
//     if (!bootstrap.has_value() || bootstrap->getToggles().empty()) {
//         return;
//     }
//     if (_config.bootstrapOverride() || !p_hasStoredToggles)
//     {
//         const ToggleSet bootstrapToggles(bootstrap->getToggles());
//         _flagStore.replace(std::make_shared<ToggleSet>(bootstrapToggles));
//         persistToggles(bootstrapToggles);
//     }
// }

void UnleashClient::initializeToggleCache() {
    auto bootstrap = _config.bootstrap();
    bool bootstrapValid = false;
    if (_config.bootstrapOverride() && bootstrap.has_value() && !bootstrap->getToggles().empty()) {
        const ToggleSet bootstrapToggles(bootstrap->getToggles());
        _flagStore.replace(std::make_shared<ToggleSet>(bootstrapToggles));
        bootstrapValid = true;
    }

    auto storage = _config.storageProvider();

    const auto cachedToggles = storage->get();
    if (cachedToggles.has_value() && cachedToggles->size() > 0) {
        _flagStore.replace(std::make_shared<ToggleSet>(*cachedToggles));
    } else {
        if (bootstrapValid)
            persistToggles(ToggleSet{bootstrap->getToggles()});
    }
}

void UnleashClient::persistToggles(const ToggleSet& p_toggles) {
    if (auto storage = _config.storageProvider()) {
        storage->save(p_toggles);
    }
}

void UnleashClient::featurePollingLoop() {
    if (!_config.isRefreshEnabled())
        return;

    const auto interval = _config.refreshInterval();

    // ---- Initial poll
    {
        Context snapshot = [&] {
            std::lock_guard<std::mutex> lk(_mutexPolling);
            return _context;
        }();
        singleFetchToggles(snapshot);
    }

    while (!_exitThreads.load(std::memory_order_acquire)) {
        Context snapshot = [&] {
            std::unique_lock<std::mutex> lock(_mutexPolling);

            _cvPolling.wait_for(lock, interval,
                                [this] { return _exitThreads.load(std::memory_order_acquire) || _contextUpdated; });

            if (_exitThreads.load(std::memory_order_acquire))
                return _context;

            _contextUpdated = false;
            return _context;
        }();

        if (_exitThreads.load(std::memory_order_acquire))
            break;
        singleFetchToggles(snapshot);
    }
}

void UnleashClient::singleFetchToggles(const Context& p_ctx) {
    auto fetchResult = _toggleFetcher.fetch(p_ctx);

    if (fetchResult.error.has_value()) {

        // define a logging strategy here!

        // emit onError(...)
        _eventHandler->emitError(
            EventHandler::ClientError{"Toggle fetch failed", std::move(fetchResult.error.value())});

        // update sdk status!!
        if (_sdkState == SdkState::Started || _sdkState == SdkState::Healthy) {
            _sdkState = SdkState::Error;
        }

        return;
    }
    if (fetchResult.status == 304) {
        // Success but no update
        return;
    }
    if ((fetchResult.status >= utils::httpStatusOkLower && fetchResult.status < utils::httpStatusOkUpper)) {
        if (fetchResult.toggles.has_value()) {
            persistToggles(fetchResult.toggles.value());

            _flagStore.replace(std::make_shared<unleash::ToggleSet>(std::move(fetchResult.toggles.value())));

            if (!_ready.exchange(true, std::memory_order_acq_rel)) {
                _eventHandler->emitReady();
            }
            // emit UpdateEvent:
            _eventHandler->emitUpdate();
        } else {
            // define a logging strategy here!
        }

    } else {
        // define a logging strategy here!
        //  emit onError(...)
        _eventHandler->emitError(
            EventHandler::ClientError{"Toggle fetch failed", std::move(fetchResult.error.value())});
    }
}

void UnleashClient::metricSendingLoop() {
    if (!_config.isMetricsEnabled())
        return;

    auto waitAndMaybeSend = [&](std::chrono::milliseconds timeout) -> bool {
        std::unique_lock<std::mutex> lock(_mutexMetrics);
        _cvMetrics.wait_for(lock, timeout, [this] { return _exitThreads.load(std::memory_order_acquire); });

        if (_exitThreads.load(std::memory_order_acquire))
            return false;

        lock.unlock();
        auto res = sendMetrics();
        // handle response...
        if (res.has_value()) {
            if (res.value().error.has_value()) {
                // Emit error signal:
                _eventHandler->emitError(EventHandler::ClientError{"Metric sending failed", res.value().error.value()});
            }
        }
        return true;
    };

    const auto initialDelay = _config.metricsIntervalInitial();
    const auto interval = _config.metricsInterval();

    if (initialDelay.count() > 0) {
        if (!waitAndMaybeSend(initialDelay))
            return;
    }

    // Periodic phase
    while (!_exitThreads.load(std::memory_order_acquire)) {
        if (!waitAndMaybeSend(interval))
            break;
    }
}

std::optional<MetricSender::MetricResult> UnleashClient::sendMetrics() {
    if (_metricStore.empty()) {
        _metricStore.reset();
        return std::nullopt;
    }
    auto jsonMetricsPayload = _metricStore.toJsonMetricsPayload();

    MetricSender::MetricResult res;
    res = _metricSender.sendMetrics(jsonMetricsPayload.value());
    _metricStore.reset();
    return res;
}

bool UnleashClient::isRunning() const noexcept {
    return _running.load(std::memory_order_acquire);
}

bool UnleashClient::isReady() const noexcept {
    return _ready.load(std::memory_order_acquire);
}

bool UnleashClient::isEnabled(const std::string& flagName) {
    if (!this->isReady())
        return false;
    auto toggleSet = _flagStore.snapshot();
    if (!toggleSet || !toggleSet->contains(flagName))
        return false;
    bool enabled = toggleSet->isEnabled(flagName);
    _metricStore.addEnableMetric(flagName, enabled);
    bool impression = toggleSet->impressionData(flagName);
    if (this->_config.impressionDataAll() || impression) {
        // Impression event emission
        unleash::Context ctx;
        {
            std::lock_guard<std::mutex> lk(_mutexPolling);
            ctx = _context;
        }

        _eventHandler->emitImpression(
            unleash::EventHandler::ClientImpression{ctx, flagName, enabled, "isEnabled", impression});
    }
    return enabled;
}

Variant UnleashClient::getVariant(const std::string& flagName) {
    if (!this->isReady())
        return Variant::disabledFactory();
    auto toggleSet = _flagStore.snapshot();
    if (!toggleSet || !toggleSet->contains(flagName))
        return Variant::disabledFactory();
    bool enabled = toggleSet->isEnabled(flagName);
    Variant variant = toggleSet->getVariant(flagName);
    _metricStore.addVariantMetric(flagName, enabled, variant.name());
    bool impression = toggleSet->impressionData(flagName);
    if (this->_config.impressionDataAll() || toggleSet->impressionData(flagName)) {
        // Impression event emission
        unleash::Context ctx;
        {
            std::lock_guard<std::mutex> lk(_mutexPolling);
            ctx = _context;
        }

        _eventHandler->emitImpression(
            unleash::EventHandler::ClientImpression{ctx, flagName, enabled, "getVariant", impression, variant.name()});
    }
    return variant;
}

bool UnleashClient::impressionData(const std::string& flagName) const {
    auto toggleSet = _flagStore.snapshot();
    if (!toggleSet)
        return false;

    return toggleSet->impressionData(flagName);
}

Context UnleashClient::context() const {
    return _context;
}

void UnleashClient::updateContext(const MutableContext& p_mCtx) {
    {
        std::lock_guard<std::mutex> lk(_mutexPolling);
        _context.updateMutableContext(p_mCtx);
        _contextUpdated = true;
    }
    _cvPolling.notify_one();
}

UnleashClient& UnleashClient::onInit(EventHandler::InitCallback cb) {
    _eventHandler->onInit(cb);
    return *this;
}

UnleashClient& UnleashClient::onError(EventHandler::ErrorCallback cb) {
    _eventHandler->onError(cb);
    return *this;
}

UnleashClient& UnleashClient::onReady(EventHandler::ReadyCallback cb) {
    _eventHandler->onReady(cb);
    return *this;
}

UnleashClient& UnleashClient::onUpdate(EventHandler::UpdateCallback cb) {
    _eventHandler->onUpdate(cb);
    return *this;
}

UnleashClient& UnleashClient::onImpression(EventHandler::ImpressionCallback cb) {
    _eventHandler->onImpression(cb);
    return *this;
}

} // namespace unleash
