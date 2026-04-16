#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace sdv::platform::centercommunication::mqttproto {

// Protocol versioning
inline constexpr const char* kProtocolVersion = "1.0";

// Error codes (shared semantics across services)
enum class ErrorCode : int {
    Ok = 0,

    // Client/request errors
    InvalidJson = 1000,
    MissingField = 1001,
    InvalidField = 1002,
    UnsupportedVersion = 1003,
    UnknownCommand = 1004,

    // Processing errors
    FeatureStopFailed = 2001,
    DeployFailed = 2002,

    // Transport/runtime errors
    PublishFailed = 3001,
};

struct ErrorInfo {
    ErrorCode code{ErrorCode::Ok};
    std::string message{};
};

struct Header {
    std::string msgId;       // UUID
    std::string timestamp;   // RFC3339/ISO-8601, e.g. 2024-04-16T15:00:00Z
    std::string vehicleId;   // e.g. CAR_001
};

// Command set (stringly-typed on the wire)
// - DEPLOY_FEATURE: deploy or shadow-run a feature binary
// - KILL_SWITCH: stop a running feature immediately
inline constexpr const char* kCommandDeployFeature = "DEPLOY_FEATURE";
inline constexpr const char* kCommandKillSwitch = "KILL_SWITCH";

// Deploy payload
struct FeaturePayload {
    std::string featureName;  // e.g. ADAS_GEN2
    std::string version;      // e.g. 2.0.1
    std::string action;       // e.g. SHADOW_RUN | ACTIVATE
    std::string binaryPath;   // e.g. /usr/bin/features/adas_v2
    std::optional<std::string> currentFeature{};  // optional: currently running feature (for shadow mode)
};

// Envelope
struct MessageV1 {
    std::string protocolVersion{kProtocolVersion};
    Header header{};
    std::string command{};
    FeaturePayload payload{};
    std::optional<ErrorInfo> error{};
};

// --- JSON mapping (snake_case on the wire) ---
inline void to_json(nlohmann::json& j, const ErrorInfo& e) {
    j = nlohmann::json{
        {"code", static_cast<int>(e.code)},
        {"message", e.message},
    };
}

inline void from_json(const nlohmann::json& j, ErrorInfo& e) {
    e.code = static_cast<ErrorCode>(j.value("code", 0));
    e.message = j.value("message", "");
}

inline void to_json(nlohmann::json& j, const Header& h) {
    j = nlohmann::json{
        {"msg_id", h.msgId},
        {"timestamp", h.timestamp},
        {"vehicle_id", h.vehicleId},
    };
}

inline void from_json(const nlohmann::json& j, Header& h) {
    h.msgId = j.value("msg_id", "");
    h.timestamp = j.value("timestamp", "");
    h.vehicleId = j.value("vehicle_id", "");
}

inline void to_json(nlohmann::json& j, const FeaturePayload& p) {
    j = nlohmann::json{
        {"feature_name", p.featureName},
        {"version", p.version},
        {"action", p.action},
        {"binary_path", p.binaryPath},
    };
    if (p.currentFeature.has_value()) {
        j["current_feature"] = *p.currentFeature;
    }
}

inline void from_json(const nlohmann::json& j, FeaturePayload& p) {
    p.featureName = j.value("feature_name", "");
    p.version = j.value("version", "");
    p.action = j.value("action", "");
    p.binaryPath = j.value("binary_path", "");
    if (j.contains("current_feature") && !j.at("current_feature").is_null()) {
        p.currentFeature = j.at("current_feature").get<std::string>();
    } else {
        p.currentFeature.reset();
    }
}

inline void to_json(nlohmann::json& j, const MessageV1& m) {
    j = nlohmann::json{
        {"protocol_version", m.protocolVersion},
        {"header", m.header},
        {"command", m.command},
        {"payload", m.payload},
    };
    if (m.error.has_value()) {
        j["error"] = *m.error;
    }
}

inline void from_json(const nlohmann::json& j, MessageV1& m) {
    m.protocolVersion = j.value("protocol_version", std::string(kProtocolVersion));
    m.header = j.value("header", Header{});
    m.command = j.value("command", "");
    m.payload = j.value("payload", FeaturePayload{});
    if (j.contains("error") && !j.at("error").is_null()) {
        m.error = j.at("error").get<ErrorInfo>();
    } else {
        m.error.reset();
    }
}

struct DecodeResultV1 {
    std::optional<MessageV1> message{};
    ErrorCode errorCode{ErrorCode::Ok};
    std::string errorMessage{};
};

inline DecodeResultV1 decodeMessageV1(const std::string& jsonText) {
    DecodeResultV1 r;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonText);
    } catch (const std::exception& e) {
        r.errorCode = ErrorCode::InvalidJson;
        r.errorMessage = e.what();
        return r;
    }

    // Heuristic: v1 envelope must include header+payload at least.
    if (!j.is_object() || !j.contains("header") || !j.contains("payload")) {
        r.errorCode = ErrorCode::InvalidField;
        r.errorMessage = "Not a v1 envelope (missing header/payload)";
        return r;
    }

    MessageV1 m;
    try {
        m = j.get<MessageV1>();
    } catch (const std::exception& e) {
        r.errorCode = ErrorCode::InvalidField;
        r.errorMessage = e.what();
        return r;
    }

    if (m.protocolVersion != kProtocolVersion) {
        r.errorCode = ErrorCode::UnsupportedVersion;
        r.errorMessage = "unsupported protocol_version=" + m.protocolVersion;
        return r;
    }
    if (m.header.msgId.empty()) {
        r.errorCode = ErrorCode::MissingField;
        r.errorMessage = "missing header.msg_id";
        return r;
    }
    if (m.header.vehicleId.empty()) {
        r.errorCode = ErrorCode::MissingField;
        r.errorMessage = "missing header.vehicle_id";
        return r;
    }
    if (m.command.empty()) {
        r.errorCode = ErrorCode::MissingField;
        r.errorMessage = "missing command";
        return r;
    }

    r.message = std::move(m);
    return r;
}

inline std::string encodeMessageV1(const MessageV1& m) {
    nlohmann::json j = m;
    return j.dump();
}

}  // namespace sdv::platform::centercommunication::mqttproto

