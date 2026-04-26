# unleash-cpp-sdk

C++17 SDK for consuming [Unleash](https://www.getunleash.io/) feature flags from the Frontend API.

This SDK focuses on:
- Fetching evaluated toggles from Unleash for a `Context`
- Exposing a runtime client API (`UnleashClient`) to check flags and variants
- Tracking and sending usage metrics
- Optional bootstrap and cache-based startup behavior

## Functional overview

Runtime flow:
1. Build a `ClientConfig` and initial `Context`.
2. Construct `UnleashClient(config, context)`.
3. `start()` launches:
   - feature polling loop (`ToggleFetcher`)
   - metrics sending loop (`MetricSender`)
4. Your application calls:
   - `isEnabled(flagName)`
   - `getVariant(flagName)`
5. SDK updates local flag snapshot (`FlagStore`) and usage counters (`MetricsStore`).
6. `stop()` joins worker threads and stops async event dispatching.

Main internal modules:
- Domain model: `Context`, `MutableContext`, `Toggle`, `Variant`, `ToggleSet`
- Stores: `FlagStore`, `MetricsStore`, plus pluggable `IStorageProvider`
- Network: `HttpClient`, `ToggleFetcher`, `MetricSender`
- Serialization: `JsonCodec`
- Events: `EventHandler`

## Class inventory (project-wide)

- `UnleashClient`: SDK facade used by applications (lifecycle, evaluation, context updates, callbacks).
- `ClientConfig`: immutable-ish runtime config object with fluent setters.
- `Bootstrap`: wrapper around initial toggle map used for startup seeding.
- `Context`: full Unleash evaluation context (static + mutable parts).
- `MutableContext`: mutable subset of context fields for runtime updates.
- `Toggle`: one feature flag snapshot entry (`name`, `enabled`, `variant`, impression).
- `Variant`: variant result for a toggle.
- `Variant::Payload`: optional variant payload (`type`, `value`).
- `ToggleSet`: map-backed container for all current toggles.
- `FlagStore`: thread-safe atomic snapshot holder for `ToggleSet`.
- `IStorageProvider`: persistence extension point for toggles.
- `LocalStorageProvider`: default no-op storage provider.
- `FileStorageProvider`: optional file-backed storage provider.
- `MetricToggle`: counters for one toggle (`yes`, `no`, variant stats).
- `MetricList`: collection of `MetricToggle` objects.
- `MetricsStore`: thread-safe in-memory metrics window and payload builder.
- `ToggleFetcher`: fetches toggles from frontend API and handles ETag/304.
- `MetricSender`: sends metrics payloads to metrics endpoint.
- `HttpRequest`: concrete transport request DTO for HTTP.
- `HttpResponse`: concrete transport response DTO for HTTP.
- `HttpClient`: `libcurl` HTTP transport implementation.
- `IComRequest`: transport-agnostic request base interface.
- `IComResponse`: transport-agnostic response base interface.
- `IComClient`: transport-agnostic client interface (`request()`).
- `ErrorResponse`: typed error response for transport/client mismatches.
- `EventHandler`: async callback queue and dispatch thread.
- `JsonCodec` (internal): JSON encode/decode helper for context, toggles, metrics.

Enums used across the SDK:
- `SdkState`
- `ClientEvent`
- `ComErrorEnum`

## Public API: `UnleashClient`

Header: `include/unleash/Client/unleashClient.hpp`

### Lifecycle
- `UnleashClient(ClientConfig, Context)`: builds stores/senders/fetcher and initializes cache/bootstrap.
- `start()`: starts event handler thread, feature polling thread, metrics thread.
- `stop()`: signals exit, notifies condition variables, joins threads, stops event handler.
- `isRunning()`: true after startup until stopped.
- `isReady()`: true once a toggle snapshot is available in `FlagStore`.

### Evaluation
- `bool isEnabled(const std::string& flagName)`
  - Returns `false` if client is not ready, flag set is missing, or flag is missing.
  - On success, records enable metric and may emit impression event.
- `Variant getVariant(const std::string& flagName)`
  - Returns `Variant::disabledFactory()` if not ready or flag missing.
  - On success, records variant metric and may emit impression event.
- `bool impressionData(const std::string& flagName) const`
  - Reads per-flag impression setting from current snapshot.

### Context management
- `Context context() const`: returns current context copy.
- `updateContext(const MutableContext&)`:
  - Replaces mutable context fields (`userId`, `remoteAddress`, `currentTime`, custom properties)
  - Wakes polling loop so next fetch uses updated context

### Events
Callback setters return `UnleashClient&` for chaining:
- `onInit(...)`
- `onError(...)`
- `onReady(...)`
- `onUpdate(...)`
- `onImpression(...)`

`EventHandler` dispatches callbacks asynchronously on its own thread and limits queue growth (`utils::maxEventQueueSize`, currently 30).

## Configuration: `ClientConfig`

Header: `include/unleash/Configuration/clientConfig.hpp`

### Required constructor fields
- `url`: Unleash frontend endpoint base URL
- `clientKey`: client/API key
- `appName`: application name (falls back to `utils::defaultAppName` if empty)

### Important options
- Polling/metrics:
  - `setRefreshInterval(seconds)` (`0`, the default value, disables polling)
  - `setMetricsInterval(seconds)` (`0`, the default value, disables metrics thread)
  - `setMetricsIntervalInitial(seconds)` (initial delay before first metrics send, with `0`, the default value, disabling the initial metric sending)
- Bootstrap/cache:
  - `setBootstrap(Bootstrap)`
  - `setBootstrapOverride(bool)` (default `true`)
  - `setStorageProvider(std::shared_ptr<IStorageProvider>)`
- HTTP headers/requesting:
  - `setHeaderName(...)` (default: `"authorization"`)
  - `setCustomHeaders(...)`
  - `setUsePostRequests(bool)` (for feature fetch requests)
  - `setTimeOutQueryMS(milliseconds)`
- Impression:
  - `setImpressionDataAll(bool)` (force all flags to emit impression events)
- Identity:
  - `setInstanceId(...)`
  - `connectionId()` auto-generated UUID used in request headers

### Validation helpers
- `isRefreshEnabled()`: `refreshInterval > 0`
- `isMetricsEnabled()`: `metricsInterval > 0`
- `isValid()`: checks mandatory fields and interval sanity (nice to check before using the client config in unleash client)

## Domain layer

### `Context`
Header: `include/unleash/Domain/context.hpp`

Represents evaluation context sent to Unleash:
- Static/identity fields:
  - `appName` (required, defaulted if empty)
  - `sessionId` (auto-generated when missing)
  - optional `environment`
- Mutable fields:
  - `userId`, `remoteAddress`, `currentTime`
  - custom `properties` (`vector<pair<string,string>>`)

Key methods:
- `setUserId`, `setRemoteAddress`, `setCurrentTime`, `setProperty`
- `updateMutableContext(const MutableContext&)`

### `MutableContext`
Used to update only mutable fields on a running client.

Behavior:
- Empty user/remote values clear the field.
- `setCurrentTime()` stores current ISO-8601 local timestamp.
- `setProperty` rejects reserved keys:
  - `appName`, `environment`, `userId`, `sessionId`, `remoteAddress`, `currentTime`

### `Toggle`
Header: `include/unleash/Domain/toggle.hpp`

Feature flag snapshot entry:
- `name`
- `enabled`
- `impressionData`
- `variant` (`Variant`)

### `Variant`
Header: `include/unleash/Domain/variant.hpp`

Variant result:
- `name`
- `enabled`
- optional `Payload { type, value }`

Helpers:
- `hasPayload()`
- `disabledFactory()` returns `"disabled"` variant

### `ToggleSet`
Header: `include/unleash/Domain/toggleSet.hpp`

Map-like immutable snapshot wrapper (`unordered_map<string, Toggle>`).

APIs:
- `contains(name)`
- `isEnabled(name)` (default `false` if missing)
- `getVariant(name)` (default `Variant::disabledFactory()` if missing)
- `impressionData(name)` (default `false` if missing)

Duplicate names inserted from vectors follow first-wins behavior (`insert` without overwrite).

## Stores

### `FlagStore`
Header: `include/unleash/Store/flagStore.hpp`

Thread-safe toggle snapshot holder:
- Internally holds `shared_ptr<const ToggleSet>`
- `snapshot()` returns atomic current pointer
- `replace(newSnapshot)` atomically swaps snapshot and marks store ready
- `isReady()` indicates at least one successful snapshot load

### `MetricsStore`
Header: `include/unleash/Metrics/metricStore.hpp`

Thread-safe metrics accumulator:
- Tracks window start timestamp
- Aggregates flag yes/no counts and per-variant counts
- `toJsonMetricsPayload()` emits Unleash metrics payload JSON
- `reset()` clears counters and restarts window

Supporting classes:
- `MetricToggle`: counters for one flag
- `MetricList`: map of flag name -> `MetricToggle`

### Storage provider (`IStorageProvider`)
Header: `include/unleash/Store/storageProvider.hpp`

Pluggable persistence for toggle snapshots:
- `get() -> optional<ToggleSet>`
- `save(const ToggleSet&)`

Default provider is `LocalStorageProvider`, which is intentionally no-op.
`FileStorageProvider` is available if you want disk-backed persistence.
Filename pattern follows the Node SDK convention:
- `unleash-backup-<appName>.json`
- `/` in app name is replaced with `_`

Use `ClientConfig::setStorageProvider(...)` to opt into file persistence.

## Bootstrap and cache behavior

`UnleashClient::initializeToggleCache()` currently behaves as follows:
1. If `bootstrapOverride == true` and bootstrap is non-empty:
   - load bootstrap into `FlagStore`
2. Read from `storageProvider->get()`:
   - if cached toggles exist, cache replaces current snapshot
3. If no cache and bootstrap was loaded:
   - bootstrap is persisted via `storageProvider->save(...)`

## Transport, fetch, and metrics sending

- `HttpClient` (`libcurl`) performs GET/POST requests and normalizes response headers to lowercase.
- `ToggleFetcher`:
  - sends context JSON body
  - decodes `toggles` response via `JsonCodec`
  - handles ETag / `If-None-Match` and 304 behavior
- `MetricSender`:
  - sends metrics to `<config.url>/client/metrics`
  - builds headers from config and returns status/error details

## JSON codec

`JsonCodec` maps domain <-> wire format:
- `decodeClientFeaturesResponse(...)` -> `ToggleSet`
- `encodeContextRequestBody(...)`
- `encodeMetricsRequestBody(...)`

## Build and test

## Examples

This repository now includes runnable examples in `examples/` that link directly to the `unleash_sdk` target.

- `live_example`: live polling example using environment variables.

### Build examples

Examples are built by default. To disable them, configure with:

```bash
cmake -S . -B build -DUNLEASH_BUILD_EXAMPLES=OFF
```

Build all targets:

```bash
cmake --build build
```

### Run example: bootstrap

```bash
./build/examples/unleash_example_bootstrap
```

### Run example: live Unleash connection

Set required environment variables and run:

```bash
export UNLEASH_URL="https://your-unleash-instance/api/frontend"
export UNLEASH_API_KEY="your-client-key"
export UNLEASH_FEATURE_NAME="my-flag"   # optional
./build/examples/live_example
```

The live example prints init/ready/update/error events and continuously prints flag/variant evaluation results once per second.

### Requirements:
- C++17 compiler
- python3 (necessary for venv and conan install)
- CMake >= 3.16
- `nlohmann_json`
- `libcurl`
- `GTest` (if building tests)

### Build:
1. Create and activate a Python virtual environment for Conan:

   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
    ```
2. Install Conan inside .venv:

   ```bash
   python -m pip install --upgrade pip
   python -m pip install conan
    ```
3. Ensure Conan profile exists :

    ```bash
    conan profile detect --force
    ```
4. Install dependencies with into build/release :

    ```bash
    conan install . -of=build/release -s build_type=Release --build=missing
    ```
5. Configure CMake using Conan toolchain:

    ```bash
    cmake -S . -B build/release -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/release/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DUNLEASH_BUILD_TESTS=ON
    ```
6. Build the sdk:
    ```bash
    cmake --build build/release -j8 #You can adapt option -j8 to fit your machine (you can check number of thread on you machine using 'echo $(nproc)'
                                    #or build directly using: ' cmake --build build/release -j"$(nproc)" ')
    ```

### Run tests:
```bash
ctest --test-dir build --output-on-failure
```





## Minimal usage example

```cpp
#include <iostream>
#include "unleash/Client/unleashClient.hpp"

using namespace utils;



int main() {
    const std::string flagName = "test-json";

    std::cout<<"sdk version is: "<<sdkVersion<<std::endl;
    const std::string appName = "test-sdk-app";
    const std::string url = "http://localhost:4242/api/frontend";
    const std::string clientKey = "default:development.unleash-insecure-frontend-api-token";
    const std::string environment = "development";

    unleash::ClientConfig config(url, clientKey, appName);
    config.setTimeOutQueryMS(std::chrono::milliseconds(5000))
          .setRefreshInterval(seconds(5))
          .setMetricsInterval(seconds(10))
          .setMetricsIntervalInitial(seconds(5))
          .setImpressionDataAll(true);

    unleash::Context context;

    unleash::UnleashClient client(config, context);
    client.onError([](const unleash::EventHandler::ClientError& e)
              {
                std::cout<<"*****on Error***** Content====> message: "<<e.message<<"/ detail: "<<e.details<<std::endl;
              }
    );

    client.onInit([]()
              {
                std::cout<<"*****on Init***** Content====> Client has initialized."<<std::endl;
              }
    ).onReady([]()
              {
                std::cout<<"*****on Ready***** Content====> Client is ready!"<<std::endl;
              }
    ).onUpdate([]()
              {
                std::cout<<"*****on Update***** Content====> toggles got updated"<<std::endl;
              }
    ).onImpression([](const unleash::EventHandler::ClientImpression& e)
              {
                std::cout<<"*****on Impression***** Content====> contextName: "<<e.ctx.getAppName()<<"\n"
                         <<"                               | flagName: "<<e.flagName<<"\n"
                         <<"                               | enabled: "<<(e.enabled? "true":"false")<<"\n"
                         <<"                               | eventType: "<<e.eventType<<"\n"
                         <<"                               | impressionData: "<<(e.impressionData? "true":"false")<<"\n"
                         <<"                               | variant: "<<(e.variant.empty()? "(empty)": e.variant)<<"\n";
              }

    );

    std::cout<<"###############Client will start now:-"<<std::endl;
    client.start();

    for(int i = 0 ; i < 5; i++)
    {
      bool enabled = client.isEnabled(flagName);
      unleash::Variant variant = client.getVariant(flagName);
      if(enabled)
      {
        std::cout<<"(Check for "<<flagName<<"): this flag is enabled |";
        if(!variant.enabled()) std::cout<<"No variant defined..."<<std::endl;
        else
        {
          std::cout<<"Variant with name: "<<variant.name()<<"| ";
          if(!variant.hasPayload()) std::cout<<"with no payload."<<std::endl;
          else
          {
            unleash::Variant::Payload p = variant.payload().value();
            std::cout<<"with payload (type:"<<p.type()<<"/value:"<<p.value()<<")"<<std::endl;
          }
        }
      }
      else std::cout<<"(Check for "<<flagName<<"): this flag is not enabled..."<<std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    client.stop();
    std::cout<<"###############Client has stopped now"<<std::endl;

    return 0;
}
```
