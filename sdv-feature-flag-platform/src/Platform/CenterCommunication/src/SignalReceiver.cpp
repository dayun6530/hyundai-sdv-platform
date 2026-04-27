#include "CenterCommunication/SignalReceiver.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>

#include "FeatureCommunication/EventBus/EventBusProxy.h"
#include "FeatureCommunication/EventBus/Event.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace {

static std::string getenvOr(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') return fallback;
    return std::string(v);
}

static void appendLogLine(const std::string& fileName, const std::string& line) {
    const std::string dir = getenvOr("SDV_LOG_DIR", "logs");
    std::ofstream out(dir + "/" + fileName, std::ios::app);
    if (!out.is_open()) return;
    out << line << std::endl;
}

static std::string trim(std::string s) {
    auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && isWs(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isWs(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

static std::unordered_set<std::string> loadDeclaredFeaturesFromLifecycleConfig() {
    std::unordered_set<std::string> features;
    const std::string path = getenvOr("SDV_LIFECYCLE_CONFIG", "");
    if (path.empty()) return features;

    std::ifstream in(path);
    if (!in.is_open()) return features;

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (startsWith(line, "feature ")) {
            std::string feat = trim(line.substr(std::string("feature ").size()));
            if (!feat.empty()) features.insert(std::move(feat));
        }
    }
    return features;
}

static std::vector<std::string> expandKillTarget(const std::string& target,
                                                 const std::unordered_set<std::string>& declaredFeatures) {
    // If the user already specified an exact feature name, keep it.
    if (declaredFeatures.empty()) return {target};
    if (declaredFeatures.find(target) != declaredFeatures.end()) return {target};

    // Shorthand: "AdasFeature" → pick the concrete configured variant (A/B) for this car.
    std::vector<std::string> expanded;
    const std::string a = target + "_A";
    const std::string b = target + "_B";
    if (declaredFeatures.find(a) != declaredFeatures.end()) expanded.push_back(a);
    if (declaredFeatures.find(b) != declaredFeatures.end()) expanded.push_back(b);
    if (!expanded.empty()) return expanded;

    // Also support "NavigationFeature" etc where actual name might be already suffixed differently.
    return {target};
}

/// e.g. `AdasFeature.FCA` -> `AdasFeature_A.FCA`, `AdasFeature_B.FCA` when lifecycle lists those.
static std::vector<std::string> expandFlagOrSubTarget(const std::string& rest,
                                                    const std::unordered_set<std::string>& declaredFeatures) {
    const std::string t = trim(rest);
    const auto dot = t.find('.');
    if (dot == std::string::npos) {
        return expandKillTarget(t, declaredFeatures);
    }
    const std::string base = trim(t.substr(0, dot));
    const std::string sub = trim(t.substr(dot + 1));
    if (base.empty() || sub.empty()) {
        return {};
    }
    const std::vector<std::string> bases = expandKillTarget(base, declaredFeatures);
    std::vector<std::string> out;
    out.reserve(bases.size());
    for (const auto& b : bases) {
        out.push_back(b + "." + sub);
    }
    return out;
}

/// Mirrors logical UI feature names (NavigationFeature / AdasFeature / EvEnergyFeature), not *_A/_B.
struct LogicalFlagSnapshot {
    std::unordered_map<std::string, bool> mainOn;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> subVal;
    /// Logical names (e.g. AdasFeature) for which KILL was applied this SignalReceiver lifetime.
    /// FLAG_ON must not revive them in MQTT state until the whole stack restarts (new receiver process).
    std::unordered_set<std::string> killedLogical;

    bool main(const std::string& logical) const {
        const auto it = mainOn.find(logical);
        if (it == mainOn.end()) {
            return true;
        }
        return it->second;
    }
    bool sub(const std::string& logical, const std::string& key) const {
        const auto it = subVal.find(logical);
        if (it == subVal.end()) {
            return true;
        }
        const auto j = it->second.find(key);
        if (j == it->second.end()) {
            return true;
        }
        return j->second;
    }
    void setMain(const std::string& logical, bool on) {
        if (on) {
            mainOn[logical] = true;
            subVal.erase(logical);
        } else {
            mainOn[logical] = false;
        }
    }
    void setSub(const std::string& logical, const std::string& key, bool on) {
        if (on) {
            subVal[logical].erase(key);
            if (subVal[logical].empty()) {
                subVal.erase(logical);
            }
        } else {
            subVal[logical][key] = false;
        }
    }
    void applyFlagOnRest(const std::string& rest) {
        const std::string t = trim(rest);
        const auto dot = t.find('.');
        if (dot == std::string::npos) {
            setMain(t, true);
            return;
        }
        const std::string base = trim(t.substr(0, dot));
        const std::string key = trim(t.substr(dot + 1));
        if (!base.empty() && !key.empty()) {
            setSub(base, key, true);
        }
    }
    void applyFlagOffRest(const std::string& rest) {
        const std::string t = trim(rest);
        const auto dot = t.find('.');
        if (dot == std::string::npos) {
            setMain(t, false);
            return;
        }
        const std::string base = trim(t.substr(0, dot));
        const std::string key = trim(t.substr(dot + 1));
        if (!base.empty() && !key.empty()) {
            setSub(base, key, false);
        }
    }
    void applyKillLogical(const std::string& logical) {
        const std::string log = trim(logical);
        if (log.empty()) {
            return;
        }
        setMain(log, false);
        killedLogical.insert(log);
    }

    bool isLogicalKilled(const std::string& logical) const {
        const std::string log = trim(logical);
        return !log.empty() && killedLogical.find(log) != killedLogical.end();
    }

    /// `AdasFeature`, `AdasFeature.FCA`, `NavigationFeature.LOCATION`, ...
    static std::string logicalBaseFromFlagRest(const std::string& rest) {
        const std::string t = trim(rest);
        const auto dot = t.find('.');
        if (dot == std::string::npos) {
            return t;
        }
        return trim(t.substr(0, dot));
    }
};

static std::string toLogicalFeatureName(std::string name) {
    name = trim(name);
    if (name.size() >= 2) {
        const std::string suf = name.substr(name.size() - 2);
        if (suf == "_A" || suf == "_B") {
            return name.substr(0, name.size() - 2);
        }
    }
    return name;
}

struct RxPublishContext {
    LogicalFlagSnapshot* mirror = nullptr;
    mqtt::async_client* mqtt = nullptr;
    std::string carId;
    std::string stateTopic;
};

static nlohmann::json buildStateJson(const std::string& carId, const LogicalFlagSnapshot& s) {
    using nlohmann::json;
    json j;
    j["protocol_version"] = "1.0";
    j["car_id"] = carId;
    j["source"] = "SignalReceiver";
    j["features"] = json::object();

    const std::vector<std::pair<std::string, std::vector<std::string>>> schema = {
        {"NavigationFeature", {"LOCATION", "ROAD", "GUIDANCE"}},
        {"AdasFeature", {"FCA", "LKA", "SCC"}},
        {"EvEnergyFeature", {"REGEN", "RANGE", "ECO"}},
    };
    for (const auto& entry : schema) {
        const std::string& log = entry.first;
        json f;
        const bool mainOn = s.main(log);
        f["main"] = mainOn;
        json subs = json::object();
        // When main is OFF or KILL latched to false, sub-toggles are inactive — expose all subs as
        // false so the UI does not keep prior ON styling for LOCATION/FCA/etc.
        for (const auto& k : entry.second) {
            subs[k] = mainOn ? s.sub(log, k) : false;
        }
        f["sub"] = subs;
        j["features"][log] = f;
    }
    return j;
}

static void publishFlagState(mqtt::async_client& client,
                             const std::string& topic,
                             const std::string& carId,
                             const LogicalFlagSnapshot& snap) {
    if (topic.empty()) {
        return;
    }
    try {
        const nlohmann::json j = buildStateJson(carId, snap);
        mqtt::message_ptr msg = mqtt::make_message(topic, j.dump());
        msg->set_qos(1);
        msg->set_retained(true);
        client.publish(msg)->wait_for(std::chrono::seconds(2));
    } catch (const std::exception& e) {
        std::cerr << "[SignalReceiver] flags/state publish failed: " << e.what() << std::endl;
    }
}

static bool forwardControlToEventBus(sdv::platform::communications::eventbus::EventBusProxy& proxy,
                                    const std::string& line,
                                    const std::unordered_set<std::string>& declaredFeatures,
                                    RxPublishContext* rx = nullptr) {
    using sdv::platform::communications::eventbus::Event;
    using sdv::platform::communications::eventbus::EventId;

    const auto publishIf = [&]() {
        if (rx && rx->mirror && rx->mqtt && !rx->stateTopic.empty()) {
            publishFlagState(*rx->mqtt, rx->stateTopic, rx->carId, *rx->mirror);
        }
    };

    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') return true;

    appendLogLine("SignalReceiver.log", std::string("RX ") + t);

    // Unified protocol: accept Signal UI v1 JSON payloads directly.
    // Example (from Signal UI):
    // {"protocol_version":"1.0","header":{"msg_id":"...","timestamp":"...","vehicle_id":"car_1"},
    //  "command":"KILL_SWITCH","payload":{"feature_name":"AdasFeature"}}
    if (!t.empty() && t.front() == '{') {
        try {
            const auto j = nlohmann::json::parse(t);
            const std::string cmd = j.value("command", "");
            if (cmd == "KILL_SWITCH") {
                std::string feature;
                if (j.contains("payload") && j["payload"].is_object()) {
                    feature = j["payload"].value("feature_name", "");
                    if (feature.empty()) {
                        feature = j["payload"].value("logical_name", "");
                    }
                }
                feature = trim(feature);
                if (feature.empty()) return false;
                for (const auto& resolved : expandKillTarget(feature, declaredFeatures)) {
                    std::cout << "[SignalReceiver] Received Message:KILL:" << resolved << std::endl;
                    if (!proxy.sendEvent(Event(EventId::KillSwitch, resolved))) return false;
                }
                if (rx && rx->mirror) {
                    rx->mirror->applyKillLogical(toLogicalFeatureName(feature));
                }
                publishIf();
                return true;
            }

            // Allow future JSON controls to coexist without breaking receivers.
            std::cerr << "[SignalReceiver] Unknown inbound JSON command ignored: " << cmd << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[SignalReceiver] Inbound JSON parse failed; falling back to legacy format. err="
                      << e.what() << std::endl;
        }
    }

    const auto colon = t.find(':');
    const std::string type = (colon == std::string::npos) ? t : t.substr(0, colon);
    const std::string rest = (colon == std::string::npos) ? "" : t.substr(colon + 1);

    if (type == "KILL" || type == "KILL_SWITCH") {
        const std::string target = trim(rest);
        if (target.empty()) return false;
        for (const auto& resolved : expandKillTarget(target, declaredFeatures)) {
            std::cout << "[SignalReceiver] Received Message:KILL:" << resolved << std::endl;
            if (!proxy.sendEvent(Event(EventId::KillSwitch, resolved))) return false;
        }
        if (rx && rx->mirror) {
            for (const auto& resolved : expandKillTarget(target, declaredFeatures)) {
                rx->mirror->applyKillLogical(toLogicalFeatureName(resolved));
            }
        }
        publishIf();
        return true;
    }

    if (type == "FLAG_ON") {
        const std::string target = trim(rest);
        if (target.empty()) return false;
        const std::string logicalBase = LogicalFlagSnapshot::logicalBaseFromFlagRest(target);
        const std::string logical = toLogicalFeatureName(logicalBase);
        if (rx && rx->mirror && rx->mirror->isLogicalKilled(logical)) {
            std::cout << "[SignalReceiver] FLAG_ON ignored (KILL latched for " << logical << "): " << target
                      << std::endl;
            return true;
        }
        const auto resolvedList = expandFlagOrSubTarget(target, declaredFeatures);
        if (resolvedList.empty()) return false;
        for (const auto& resolved : resolvedList) {
            if (!proxy.sendEvent(Event(EventId::FeatureFlagOn, resolved))) return false;
        }
        if (rx && rx->mirror) {
            rx->mirror->applyFlagOnRest(target);
        }
        publishIf();
        return true;
    }
    if (type == "FLAG_OFF") {
        const std::string target = trim(rest);
        if (target.empty()) return false;
        const auto resolvedList = expandFlagOrSubTarget(target, declaredFeatures);
        if (resolvedList.empty()) return false;
        for (const auto& resolved : resolvedList) {
            if (!proxy.sendEvent(Event(EventId::FeatureFlagOff, resolved))) return false;
        }
        if (rx && rx->mirror) {
            rx->mirror->applyFlagOffRest(target);
        }
        publishIf();
        return true;
    }
    if (type == "SHADOW_ON") {
        const std::string target = trim(rest);
        if (target.empty()) return false;
        return proxy.sendEvent(Event(EventId::ShadowModeOn, target));
    }
    if (type == "SHADOW_OFF") {
        const std::string target = trim(rest);
        if (target.empty()) return false;
        return proxy.sendEvent(Event(EventId::ShadowModeOff, target));
    }
    if (type == "SHADOW_PROMOTE") {
        const std::string target = trim(rest);
        if (target.empty()) return false;
        return proxy.sendEvent(Event(EventId::ShadowPromotionRequest, target));
    }
    if (type == "AB_SET") {
        // Format: AB_SET:<feature>:A|B
        const auto colon2 = rest.find(':');
        if (colon2 == std::string::npos) return false;
        const std::string feat = trim(rest.substr(0, colon2));
        const std::string variant = trim(rest.substr(colon2 + 1));
        if (feat.empty() || (variant != "A" && variant != "B")) return false;
        return proxy.sendEvent(Event(EventId::AbVariantAssign, feat + ":" + variant));
    }

    // Unknown signal types are ignored (forward-compatible).
    std::cerr << "[SignalReceiver] Unknown inbound signal ignored: " << t << std::endl;
    return true;
}

}  // namespace

namespace sdv::platform::centercommunication {

SignalReceiver::SignalReceiver(const std::string& centerUrl,
                               const std::string& signalFilePath)
    : Signal(centerUrl, signalFilePath) {}

void SignalReceiver::start() {
    running_ = true;
    std::cout << "[SignalReceiver] Started (MQTT->EventBus). " << std::endl;

    while (running_) {
        pollExternalSignals();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    std::cout << "[SignalReceiver] Stopped." << std::endl;
}

void SignalReceiver::stop() {
    running_ = false;
}

void SignalReceiver::pollExternalSignals() {
    // MQTT message consumption is handled in main() to keep a single long-lived client.
}

}  // namespace sdv::platform::centercommunication

static std::atomic<bool> shutdownRequested(false);

void signalReceiverHandler(int) {
    shutdownRequested = true;
}

#ifndef SDV_CENTERCOMM_NO_MAIN
int main(int argc, char** argv) {
    signal(SIGTERM, signalReceiverHandler);
    signal(SIGINT, signalReceiverHandler);

    // LifeCycleMgr passes:
    // - argv[1]: connect-friendly EventBus address for clients (e.g. 127.0.0.1:50051)
    // - argv[last]: car_<n> instance id (when running car_1..car_10)
    const std::string eventBusAddress =
        (argc > 1 && argv[1] && *argv[1] != '\0') ? std::string(argv[1]) : std::string("127.0.0.1:50051");
    std::string carId = "car_1";
    if (argc > 1 && argv[argc - 1]) {
        const std::string last = std::string(argv[argc - 1]);
        if (last.rfind("car_", 0) == 0) carId = last;
    }
    const std::string brokerUri = getenvOr("SDV_MQTT_BROKER_URI", "tcp://127.0.0.1:1884");
    const std::string subscribeTopic = getenvOr(
        "SDV_MQTT_SIGNAL_RX_TOPIC",
        "/sdv/vehicle/" + carId + "/signals/inbound");
    const std::string stateTopic =
        getenvOr("SDV_MQTT_FLAGS_STATE_TOPIC", "/sdv/vehicle/" + carId + "/flags/state");

    auto declaredFeatures = loadDeclaredFeaturesFromLifecycleConfig();

    sdv::platform::communications::eventbus::EventBusProxy proxy(eventBusAddress);
    const int64_t componentId = proxy.registerComponent();
    if (componentId < 0) {
        std::cerr << "[SignalReceiver] EventBus registerComponent failed" << std::endl;
        return 2;
    }

    const std::string clientId = "sdv-" + carId + "-signal-rx";
    mqtt::async_client client(brokerUri, clientId);
    LogicalFlagSnapshot mirror;
    RxPublishContext rxCtx;
    rxCtx.mirror = &mirror;
    rxCtx.mqtt = &client;
    rxCtx.carId = carId;
    rxCtx.stateTopic = stateTopic;

    mqtt::connect_options conn;
    conn.set_clean_session(true);
    conn.set_keep_alive_interval(30);

    try {
        client.connect(conn)->wait();
        client.start_consuming();
        client.subscribe(subscribeTopic, 1)->wait();
        publishFlagState(client, stateTopic, carId, mirror);
        std::cout << "[SignalReceiver] Connected. broker=" << brokerUri
                  << " topic=" << subscribeTopic
                  << " stateTopic=" << stateTopic
                  << " eventBus=" << eventBusAddress << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[SignalReceiver] MQTT connect/subscribe failed: " << e.what() << std::endl;
        proxy.unRegister(componentId);
        return 3;
    }

    while (!shutdownRequested) {
        auto msg = client.try_consume_message_for(std::chrono::milliseconds(200));
        if (!msg) continue;

        const std::string payload = msg->to_string();
        const bool ok = forwardControlToEventBus(proxy, payload, declaredFeatures, &rxCtx);
        if (!ok) {
            std::cerr << "[SignalReceiver] Failed to forward inbound signal: " << payload << std::endl;
        }
    }

    try {
        client.unsubscribe(subscribeTopic)->wait();
        client.stop_consuming();
        client.disconnect()->wait();
    } catch (...) {
    }
    proxy.unRegister(componentId);

    return 0;
}
#endif  // SDV_CENTERCOMM_NO_MAIN
