#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <thread>
#include <optional>
#include "unleash/EventHandler/eventHandler.hpp"
#include "unleash/Configuration/clientConfig.hpp"
#include "unleash/Domain/context.hpp"
#include "unleash/Domain/toggleSet.hpp"
#include "unleash/Domain/variant.hpp"
#include "unleash/EventHandler/eventHandler.hpp"
#include "unleash/Store/flagStore.hpp"
#include "unleash/Metrics/metricStore.hpp"
#include "unleash/Metrics/metricSender.hpp"
#include "unleash/Fetcher/toggleFetcher.hpp"
#include "unleash/Store/storageProvider.hpp"

namespace unleash {

enum class SdkState : std::uint8_t { Started, Healthy, Error, Stopped };

class UnleashClient {
  public:
    explicit UnleashClient(ClientConfig p_config, Context p_ctx);

    ~UnleashClient();

    UnleashClient(const UnleashClient&) = delete;
    UnleashClient& operator=(const UnleashClient&) = delete;

    void start();
    void stop();

    bool isRunning() const noexcept;
    bool isReady() const noexcept;

    bool isEnabled(const std::string& flagName);

    Variant getVariant(const std::string& flagName);
    bool impressionData(const std::string& flagName) const;

    // Context handlers:
    Context context() const;
    void updateContext(const MutableContext& p_mCtx);

    // Callback setters
    UnleashClient& onInit(EventHandler::InitCallback cb);
    UnleashClient& onError(EventHandler::ErrorCallback cb);
    UnleashClient& onReady(EventHandler::ReadyCallback cb);
    UnleashClient& onUpdate(EventHandler::UpdateCallback cb);
    UnleashClient& onImpression(EventHandler::ImpressionCallback cb);

  private:
    bool isStoreReady();

    void initializeToggleCache();

    // void applyBootstrap(bool hasStoredToggles);

    void persistToggles(const ToggleSet& p_toggles);

    void featurePollingLoop();

    void metricSendingLoop();

    std::optional<MetricSender::MetricResult> sendMetrics();

    void singleFetchToggles(const Context& p_ctx);

    ClientConfig _config;
    Context _context;
    // stores:
    FlagStore _flagStore;
    MetricsStore _metricStore;
    // senders:
    MetricSender _metricSender;
    // toggle Fetcher:
    ToggleFetcher _toggleFetcher;
    // even handler:
    std::shared_ptr<EventHandler> _eventHandler;

    std::atomic_bool _running{false};
    std::atomic_bool _ready{false};
    std::atomic_bool _exitThreads{false};
    bool _contextUpdated{false};

    // Thread variables:
    std::thread _fPollingThread;
    std::thread _mSendingThread;
    // multithreading Data race handling :
    std::condition_variable _cvMetrics, _cvPolling;
    std::mutex _mutexMetrics, _mutexPolling;

    // sdkState:
    SdkState _sdkState{SdkState::Stopped};
};

} // namespace unleash