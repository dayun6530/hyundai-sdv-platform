#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace mqtt {
class async_client;
class connect_options;
class message;
using const_message_ptr = std::shared_ptr<const message>;
}  // namespace mqtt

namespace sdv::platform::centercommunication {

class IFeatureController {
public:
    virtual ~IFeatureController() = default;
    virtual void stopFeature(const std::string& logicalName) = 0;
    virtual void deployFeatureBinary(const std::string& logicalName,
                                     const std::string& version,
                                     const std::string& payloadUrl) = 0;
};

class MqttVehicleAgent final {
public:
    static MqttVehicleAgent& instance();

    MqttVehicleAgent(const MqttVehicleAgent&) = delete;
    MqttVehicleAgent& operator=(const MqttVehicleAgent&) = delete;
    MqttVehicleAgent(MqttVehicleAgent&&) = delete;
    MqttVehicleAgent& operator=(MqttVehicleAgent&&) = delete;

    ~MqttVehicleAgent();

    struct Config {
        std::string brokerUri = "tcp://127.0.0.1:1883";
        std::string clientId = "sdv-vehicle-001-agent";

        std::string subscribeTopic = "/sdv/vehicle/001/control/#";
        std::string publishTopic = "/sdv/vehicle/001/telemetry";

        int qos = 1;  // QoS 1+ required
        int keepAliveSeconds = 30;
        int reconnectMinSeconds = 1;
        int reconnectMaxSeconds = 30;
        bool cleanStart = false;
    };

    void configure(Config cfg);
    void setFeatureController(std::shared_ptr<IFeatureController> controller);

    void start();
    void stop();
    bool isRunning() const;

private:
    MqttVehicleAgent();

    void eventLoop();
    void ensureConnectedAndSubscribed();
    void onMqttMessage(mqtt::const_message_ptr msg);

    void handleCommandJson(const std::string& topic, const std::string& payload);
    void handleKillSwitch(const std::string& targetFeature,
                          const std::optional<std::string>& version);
    void handleDeploy(const std::string& targetFeature,
                      const std::string& version,
                      const std::string& payloadUrl);

    void publishAck(const std::string& command,
                    const std::string& targetFeature,
                    const std::optional<std::string>& version,
                    const std::string& status,
                    const std::string& detail);

    void publishTelemetryAck(const std::string& requestId,
                             const std::string& command,
                             const std::string& targetFeature,
                             const std::optional<std::string>& version,
                             const std::string& status,
                             const std::string& detail);

    // MQTT callback glue
    class Callback;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::queue<mqtt::const_message_ptr> inbox_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread loopThread_;

    Config cfg_;
    std::shared_ptr<IFeatureController> featureController_;

    std::unique_ptr<mqtt::async_client> client_;
    std::unique_ptr<Callback> callback_;
};

}  // namespace sdv::platform::centercommunication

