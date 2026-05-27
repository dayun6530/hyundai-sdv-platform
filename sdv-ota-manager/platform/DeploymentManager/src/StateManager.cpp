#include "DeploymentManager/StateManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace sdv::platform::deployment {

namespace {

class JsonLite {
public:
    explicit JsonLite(std::string_view in) : s_(in) {}

    bool consume(char ch) {
        skipWs();
        if (!eof() && s_[i_] == ch) {
            ++i_;
            return true;
        }
        return false;
    }

    bool expect(char ch, std::string& err) {
        if (!consume(ch)) {
            err = std::string("expected '") + ch + "'";
            return false;
        }
        return true;
    }

    bool eof() const { return i_ >= s_.size(); }

    bool parseString(std::string& out, std::string& err) {
        skipWs();
        if (eof() || s_[i_] != '"') {
            err = "expected string";
            return false;
        }
        ++i_;
        std::string r;
        while (!eof()) {
            char c = s_[i_++];
            if (c == '"') {
                out = std::move(r);
                return true;
            }
            if (c == '\\') {
                if (eof()) {
                    err = "unterminated escape";
                    return false;
                }
                char e = s_[i_++];
                switch (e) {
                case '"': r.push_back('"'); break;
                case '\\': r.push_back('\\'); break;
                case '/': r.push_back('/'); break;
                case 'b': r.push_back('\b'); break;
                case 'f': r.push_back('\f'); break;
                case 'n': r.push_back('\n'); break;
                case 'r': r.push_back('\r'); break;
                case 't': r.push_back('\t'); break;
                default:
                    err = "unsupported escape sequence";
                    return false;
                }
                continue;
            }
            r.push_back(c);
        }
        err = "unterminated string";
        return false;
    }

    bool parseBool(bool& out, std::string& err) {
        skipWs();
        if (match("true")) {
            out = true;
            i_ += 4;
            return true;
        }
        if (match("false")) {
            out = false;
            i_ += 5;
            return true;
        }
        err = "expected boolean";
        return false;
    }

    bool parseInt(int& out, std::string& err) {
        skipWs();
        if (eof()) {
            err = "expected number";
            return false;
        }
        bool neg = false;
        if (s_[i_] == '-') {
            neg = true;
            ++i_;
        }
        if (eof() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) {
            err = "expected number";
            return false;
        }
        long v = 0;
        while (!eof() && std::isdigit(static_cast<unsigned char>(s_[i_]))) {
            v = v * 10 + (s_[i_] - '0');
            ++i_;
        }
        out = static_cast<int>(neg ? -v : v);
        return true;
    }

    void skipValue(std::string& err) {
        skipWs();
        if (eof()) {
            err = "unexpected EOF while skipping value";
            return;
        }
        const char c = s_[i_];
        if (c == '"') {
            std::string tmp;
            parseString(tmp, err);
            return;
        }
        if (c == '{' || c == '[') {
            // Robust composite skipper:
            // - Handles nested objects/arrays (mixed) by tracking a small "expected closing" stack.
            // - Ignores braces/brackets inside JSON strings by delegating to parseString().
            // - Best-effort only; this JsonLite parser is intentionally minimal.
            std::vector<char> closers;
            closers.reserve(8);
            auto pushCloser = [&](char open) {
                if (open == '{') closers.push_back('}');
                else if (open == '[') closers.push_back(']');
            };
            pushCloser(c);
            ++i_;  // consume initial '{' or '['

            while (!eof() && !closers.empty()) {
                const char x = s_[i_];
                if (x == '"') {
                    std::string tmp;
                    if (!parseString(tmp, err)) return;
                    continue;
                }
                if (x == '{' || x == '[') {
                    pushCloser(x);
                    ++i_;
                    continue;
                }
                if (x == closers.back()) {
                    ++i_;
                    closers.pop_back();
                    continue;
                }
                ++i_;
            }
            if (!closers.empty() && eof()) {
                err = "unexpected EOF while skipping composite value";
            }
            return;
        }
        // number / bool / null: consume until delimiter
        while (!eof()) {
            char x = s_[i_];
            if (std::isspace(static_cast<unsigned char>(x)) || x == ',' || x == ']' || x == '}') break;
            ++i_;
        }
    }

private:
    void skipWs() {
        while (!eof() && std::isspace(static_cast<unsigned char>(s_[i_]))) {
            ++i_;
        }
    }

    bool match(std::string_view lit) const {
        return s_.substr(i_, lit.size()) == lit;
    }

    std::string_view s_;
    std::size_t i_{0};
};

void indent(std::ostream& out, int n) {
    for (int i = 0; i < n; ++i) out << ' ';
}

/// Lowercase, trim spaces, strip a single leading `v` for version tokens (v1.0 / 1.0).
std::string normalizeVersionToken(std::string_view in) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (s.size() >= 2 && s[0] == 'v' && std::isdigit(static_cast<unsigned char>(s[1]))) {
        s.erase(0, 1);
    }
    return s;
}

/// `candidate` only when shadow lane is enabled and shadow version differs from active.
std::string fleetFeatureToRuntimeDomainKey(const std::string& feature) {
    if (feature == "ADAS") {
        return "adas";
    }
    if (feature == "Navigation") {
        return "navigation";
    }
    if (feature == "Energy") {
        return "evenergy";
    }
    return {};
}

std::string formatFleetVersionTag(std::string v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c); }), v.end());
    if (v.empty()) {
        return v;
    }
    if (v[0] == 'v' || v[0] == 'V') {
        return std::string("v") + v.substr(1);
    }
    return std::string("v") + v;
}

std::optional<std::string> extractBalancedJsonObject(std::string_view json, std::size_t startBrace) {
    if (startBrace >= json.size() || json[startBrace] != '{') {
        return std::nullopt;
    }
    int depth = 0;
    bool inStr = false;
    for (std::size_t i = startBrace; i < json.size(); ++i) {
        const char c = json[i];
        if (inStr) {
            if (c == '\\' && i + 1 < json.size()) {
                ++i;
                continue;
            }
            if (c == '"') {
                inStr = false;
            }
            continue;
        }
        if (c == '"') {
            inStr = true;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return std::string(json.substr(startBrace, i - startBrace + 1));
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> jsonObjectStringFieldLoose(const std::string& obj, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto kpos = obj.find(needle);
    if (kpos == std::string::npos) {
        return std::nullopt;
    }
    auto colon = obj.find(':', kpos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    JsonLite p(std::string_view(obj).substr(colon + 1));
    std::string val;
    std::string err;
    if (!p.parseString(val, err)) {
        return std::nullopt;
    }
    return val;
}

std::optional<bool> jsonObjectBoolFieldLoose(const std::string& obj, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto kpos = obj.find(needle);
    if (kpos == std::string::npos) {
        return std::nullopt;
    }
    auto colon = obj.find(':', kpos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    JsonLite p(std::string_view(obj).substr(colon + 1));
    bool val = false;
    std::string err;
    if (!p.parseBool(val, err)) {
        return std::nullopt;
    }
    return val;
}

struct VehicleRuntimeDomainView {
    std::string primary_version;
    std::string shadow_version;
    bool shadow_enabled{false};
};

bool readVehicleRuntimeDomainFromFile(const std::string& path, const std::string& domainKey,
    VehicleRuntimeDomainView& out, std::string& error) {
    error.clear();
    std::ifstream in(path);
    if (!in) {
        error = "vehicle_runtime file not found: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();

    const std::string domainsNeedle = "\"domains\"";
    const auto dpos = json.find(domainsNeedle);
    if (dpos == std::string::npos) {
        error = "vehicle_runtime missing domains object";
        return false;
    }
    const auto domainsBrace = json.find('{', dpos);
    if (domainsBrace == std::string::npos) {
        error = "vehicle_runtime domains object malformed";
        return false;
    }
    const auto domainsObj = extractBalancedJsonObject(json, domainsBrace);
    if (!domainsObj) {
        error = "vehicle_runtime domains object malformed";
        return false;
    }

    const std::string domNeedle = "\"" + domainKey + "\"";
    const auto kpos = domainsObj->find(domNeedle);
    if (kpos == std::string::npos) {
        error = "vehicle_runtime missing domain: " + domainKey;
        return false;
    }
    const auto domBrace = domainsObj->find('{', kpos);
    if (domBrace == std::string::npos) {
        error = "vehicle_runtime domain object malformed: " + domainKey;
        return false;
    }
    const auto domObj = extractBalancedJsonObject(*domainsObj, domBrace);
    if (!domObj) {
        error = "vehicle_runtime domain object malformed: " + domainKey;
        return false;
    }

    out.primary_version = jsonObjectStringFieldLoose(*domObj, "primary_version").value_or("");
    out.shadow_version = jsonObjectStringFieldLoose(*domObj, "shadow_version").value_or("");
    if (const auto se = jsonObjectBoolFieldLoose(*domObj, "shadow_enabled")) {
        out.shadow_enabled = *se;
    } else {
        out.shadow_enabled = !out.shadow_version.empty()
            && normalizeVersionToken(out.primary_version) != normalizeVersionToken(out.shadow_version);
    }
    return true;
}

std::string deriveShadowMode(const StateManager::FeatureState& f) {
    if (!f.shadow_enabled) {
        return "inactive";
    }
    const std::string sv = normalizeVersionToken(f.shadow_version);
    if (sv.empty()) {
        return "inactive";
    }
    if (normalizeVersionToken(f.active_version) == sv) {
        return "inactive";
    }
    return "candidate";
}

std::string quote(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    r.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') r.push_back('\\');
        r.push_back(c);
    }
    r.push_back('"');
    return r;
}

std::string nowUtcIsoZ() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

bool parseFeature(JsonLite& p, StateManager::FeatureState& out, const std::string& featureName, std::string& err) {
    if (!p.expect('{', err)) return false;

    // allow empty object? no, but keep parsing tolerant
    if (p.consume('}')) {
        err = "feature object is empty: " + featureName;
        return false;
    }

    while (true) {
        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        auto parseRollbackStackEntry = [&](StateManager::RollbackStackEntry& e) -> bool {
            if (!p.expect('{', err)) return false;
            if (p.consume('}')) {
                err = "rollback_stack entry object is empty: " + featureName;
                return false;
            }
            while (true) {
                std::string k;
                if (!p.parseString(k, err)) return false;
                if (!p.expect(':', err)) return false;
                if (k == "slot") {
                    if (!p.parseString(e.slot, err)) return false;
                } else if (k == "version") {
                    if (!p.parseString(e.version, err)) return false;
                } else if (k == "docker_image") {
                    if (!p.parseString(e.docker_image, err)) return false;
                } else if (k == "previous_role") {
                    if (!p.parseString(e.previous_role, err)) return false;
                } else if (k == "recorded_at") {
                    if (!p.parseString(e.recorded_at, err)) return false;
                } else if (k == "reason") {
                    if (!p.parseString(e.reason, err)) return false;
                } else if (k == "status") {
                    if (!p.parseString(e.status, err)) return false;
                } else {
                    p.skipValue(err);
                    if (!err.empty()) return false;
                }
                if (p.consume('}')) break;
                if (!p.expect(',', err)) return false;
                if (p.consume('}')) {
                    err = "trailing comma in rollback_stack entry: " + featureName;
                    return false;
                }
            }
            return true;
        };

        auto parseBlockedEntry = [&](StateManager::BlockedVersionEntry& e, const char* arrayName) -> bool {
            if (!p.expect('{', err)) return false;
            if (p.consume('}')) {
                err = std::string(arrayName) + " entry object is empty: " + featureName;
                return false;
            }
            while (true) {
                std::string k;
                if (!p.parseString(k, err)) return false;
                if (!p.expect(':', err)) return false;
                if (k == "slot") {
                    if (!p.parseString(e.slot, err)) return false;
                } else if (k == "version") {
                    if (!p.parseString(e.version, err)) return false;
                } else if (k == "docker_image") {
                    if (!p.parseString(e.docker_image, err)) return false;
                } else if (k == "blocked_at") {
                    if (!p.parseString(e.blocked_at, err)) return false;
                } else if (k == "reason") {
                    if (!p.parseString(e.reason, err)) return false;
                } else if (k == "source") {
                    if (!p.parseString(e.source, err)) return false;
                } else {
                    p.skipValue(err);
                    if (!err.empty()) return false;
                }
                if (p.consume('}')) break;
                if (!p.expect(',', err)) return false;
                if (p.consume('}')) {
                    err = std::string("trailing comma in ") + arrayName + " entry: " + featureName;
                    return false;
                }
            }
            return true;
        };

        auto parseRollbackStackArray = [&]() -> bool {
            if (!p.expect('[', err)) return false;
            out.rollback_stack.clear();
            if (p.consume(']')) return true;
            while (true) {
                StateManager::RollbackStackEntry e{};
                if (!parseRollbackStackEntry(e)) return false;
                out.rollback_stack.push_back(std::move(e));
                if (p.consume(']')) break;
                if (!p.expect(',', err)) return false;
                if (p.consume(']')) {
                    err = "trailing comma in rollback_stack array: " + featureName;
                    return false;
                }
            }
            return true;
        };

        auto parseBlockedArray = [&](std::vector<StateManager::BlockedVersionEntry>& dst, const char* arrayName) -> bool {
            if (!p.expect('[', err)) return false;
            dst.clear();
            if (p.consume(']')) return true;
            while (true) {
                StateManager::BlockedVersionEntry e{};
                if (!parseBlockedEntry(e, arrayName)) return false;
                dst.push_back(std::move(e));
                if (p.consume(']')) break;
                if (!p.expect(',', err)) return false;
                if (p.consume(']')) {
                    err = std::string("trailing comma in ") + arrayName + " array: " + featureName;
                    return false;
                }
            }
            return true;
        };

        if (key == "active_slot") {
            if (!p.parseString(out.active_slot, err)) return false;
            if (out.primary_slot.empty()) out.primary_slot = out.active_slot;
        } else if (key == "active_version") {
            if (!p.parseString(out.active_version, err)) return false;
            if (out.primary_version.empty()) out.primary_version = out.active_version;
        } else if (key == "primary_slot") {
            if (!p.parseString(out.primary_slot, err)) return false;
            if (out.active_slot.empty()) out.active_slot = out.primary_slot;
        } else if (key == "primary_version") {
            if (!p.parseString(out.primary_version, err)) return false;
            if (out.active_version.empty()) out.active_version = out.primary_version;
        } else if (key == "primary_image") {
            if (!p.parseString(out.primary_image, err)) return false;
        } else if (key == "artifact_type") {
            if (!p.parseString(out.artifact_type, err)) return false;
        } else if (key == "image_digest") {
            if (!p.parseString(out.image_digest, err)) return false;
        } else if (key == "manifest_path") {
            if (!p.parseString(out.manifest_path, err)) return false;
        } else if (key == "package_id") {
            if (!p.parseString(out.package_id, err)) return false;
        } else if (key == "docker_runtime_result") {
            if (!p.parseString(out.docker_runtime_result, err)) return false;
        } else if (key == "docker_runtime_mode") {
            if (!p.parseString(out.docker_runtime_mode, err)) return false;
        } else if (key == "docker_runtime_action") {
            if (!p.parseString(out.docker_runtime_action, err)) return false;
        } else if (key == "docker_runtime_time") {
            if (!p.parseString(out.docker_runtime_time, err)) return false;
        } else if (key == "docker_runtime_reason") {
            if (!p.parseString(out.docker_runtime_reason, err)) return false;
        } else if (key == "docker_runtime_container") {
            if (!p.parseString(out.docker_runtime_container, err)) return false;
        } else if (key == "docker_runtime_image") {
            if (!p.parseString(out.docker_runtime_image, err)) return false;
        } else if (key == "docker_runtime_pull_policy") {
            if (!p.parseString(out.docker_runtime_pull_policy, err)) return false;
        } else if (key == "pending_active_slot") {
            if (!p.parseString(out.pending_active_slot, err)) return false;
        } else if (key == "pending_active_version") {
            if (!p.parseString(out.pending_active_version, err)) return false;
        } else if (key == "flag") {
            if (!p.parseString(out.flag, err)) return false;
        } else if (key == "shadow_enabled") {
            if (!p.parseBool(out.shadow_enabled, err)) return false;
        } else if (key == "shadow_slot") {
            if (!p.parseString(out.shadow_slot, err)) return false;
        } else if (key == "shadow_version") {
            if (!p.parseString(out.shadow_version, err)) return false;
        } else if (key == "shadow_image") {
            if (!p.parseString(out.shadow_image, err)) return false;
        } else if (key == "shadow_status") {
            if (!p.parseString(out.shadow_status, err)) return false;
        } else if (key == "shadow_started_at") {
            if (!p.parseString(out.shadow_started_at, err)) return false;
        } else if (key == "shadow_validation_result") {
            if (!p.parseString(out.shadow_validation_result, err)) return false;
        } else if (key == "shadow_failure_reason") {
            if (!p.parseString(out.shadow_failure_reason, err)) return false;
        } else if (key == "shadow_mode") {
            std::string sm;
            if (!p.parseString(sm, err)) return false;
            (void)sm;  // authoritative value is re-derived on write (shadow_enabled + versions)
        } else if (key == "kill_switch") {
            if (!p.parseBool(out.kill_switch, err)) return false;
        } else if (key == "kill_reason") {
            if (!p.parseString(out.kill_reason, err)) return false;
        } else if (key == "fallback_slot") {
            if (!p.parseString(out.fallback_slot, err)) return false;
        } else if (key == "fallback_version") {
            if (!p.parseString(out.fallback_version, err)) return false;
        } else if (key == "realtime_route_flag") {
            if (!p.parseString(out.realtime_route_flag, err)) return false;
        } else if (key == "charging_recommend_flag") {
            if (!p.parseString(out.charging_recommend_flag, err)) return false;
        } else if (key == "advanced_range_flag") {
            if (!p.parseString(out.advanced_range_flag, err)) return false;
        } else if (key == "rollback_stack") {
            if (!parseRollbackStackArray()) return false;
        } else if (key == "blocked_versions") {
            if (!parseBlockedArray(out.blocked_versions, "blocked_versions")) return false;
        } else if (key == "quarantine") {
            if (!parseBlockedArray(out.quarantine, "quarantine")) return false;
        } else {
            p.skipValue(err);
            if (!err.empty()) return false;
        }

        if (p.consume('}')) break;
        if (!p.expect(',', err)) return false;
        if (p.consume('}')) {
            err = "trailing comma in feature object: " + featureName;
            return false;
        }
    }

    // Ensure alias fields are coherent even when JSON contains only one side.
    if (out.primary_slot.empty()) out.primary_slot = out.active_slot;
    if (out.primary_version.empty()) out.primary_version = out.active_version;
    if (out.active_slot.empty()) out.active_slot = out.primary_slot;
    if (out.active_version.empty()) out.active_version = out.primary_version;

    // Default: feature flag ON unless explicitly set OFF in JSON (or kill_switch below).
    if (out.flag.empty()) {
        out.flag = "ON";
    }

    // Shadow status compatibility:
    // - legacy state may only have shadow_enabled/shadow_version.
    // - treat enabled=true as RUNNING by default (validation can refine later).
    if (out.shadow_status.empty()) {
        out.shadow_status = out.shadow_enabled ? "RUNNING" : "DISABLED";
    }
    // IMPORTANT: Do NOT synthesize timestamps during parsing/load.
    // `shadow_started_at` must reflect the real "shadow ON" event time and is only written
    // by updateShadowState() / fleet-shadow command paths (not by reading state JSON).

    return true;
}

bool parseAck(JsonLite& p, StateManager::AckRecord& out, std::string& err) {
    if (!p.expect('{', err)) return false;
    if (p.consume('}')) {
        err = "last_ack object is empty";
        return false;
    }
    while (true) {
        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        if (key == "status") {
            if (!p.parseString(out.status, err)) return false;
        } else if (key == "at") {
            if (!p.parseString(out.at, err)) return false;
        } else if (key == "message") {
            if (!p.parseString(out.message, err)) return false;
        } else if (key == "feature") {
            if (!p.parseString(out.feature, err)) return false;
        } else if (key == "version") {
            if (!p.parseString(out.version, err)) return false;
        } else if (key == "deploy_id") {
            if (!p.parseString(out.deploy_id, err)) return false;
        } else {
            p.skipValue(err);
            if (!err.empty()) return false;
        }

        if (p.consume('}')) break;
        if (!p.expect(',', err)) return false;
        if (p.consume('}')) {
            err = "trailing comma in last_ack object";
            return false;
        }
    }
    return true;
}

bool parseOta(JsonLite& p, StateManager::OtaState& out, std::string& err) {
    if (!p.expect('{', err)) return false;
    if (p.consume('}')) {
        err = "ota object is empty";
        return false;
    }

    while (true) {
        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        if (key == "download_status") {
            if (!p.parseString(out.download_status, err)) return false;
        } else if (key == "install_status") {
            if (!p.parseString(out.install_status, err)) return false;
        } else if (key == "vehicle_mode") {
            if (!p.parseString(out.vehicle_mode, err)) return false;
        } else if (key == "ignition") {
            if (!p.parseString(out.ignition, err)) return false;
        } else if (key == "update_mode") {
            if (!p.parseString(out.update_mode, err)) return false;
        } else if (key == "ota_phase") {
            if (!p.parseString(out.ota_phase, err)) return false;
        } else if (key == "failure_reason") {
            if (!p.parseString(out.failure_reason, err)) return false;
        } else if (key == "gear") {
            if (!p.parseString(out.gear, err)) return false;
        } else if (key == "speed") {
            if (!p.parseInt(out.speed, err)) return false;
        } else if (key == "parking_brake") {
            if (!p.parseString(out.parking_brake, err)) return false;
        } else if (key == "battery") {
            if (!p.parseInt(out.battery, err)) return false;
        } else if (key == "driver_approved") {
            if (!p.parseBool(out.driver_approved, err)) return false;
        } else if (key == "network") {
            if (!p.parseString(out.network, err)) return false;
        } else if (key == "target_system_running") {
            if (!p.parseBool(out.target_system_running, err)) return false;
        } else {
            p.skipValue(err);
            if (!err.empty()) return false;
        }

        if (p.consume('}')) break;
        if (!p.expect(',', err)) return false;
        if (p.consume('}')) {
            err = "trailing comma in ota object";
            return false;
        }
    }
    return true;
}

bool parseVehicleState(std::string_view json, StateManager::VehicleState& out, std::string& err) {
    JsonLite p(json);
    if (!p.expect('{', err)) return false;

    bool sawFeatures = false;
    bool sawOta = false;

    while (true) {
        if (p.consume('}')) break;

        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        if (key == "car_id") {
            if (!p.parseString(out.car_id, err)) return false;
        } else if (key == "model") {
            if (!p.parseString(out.model, err)) return false;
        } else if (key == "region") {
            if (!p.parseString(out.region, err)) return false;
        } else if (key == "group") {
            if (!p.parseString(out.group, err)) return false;
        } else if (key == "status") {
            if (!p.parseString(out.status, err)) return false;
        } else if (key == "last_seen_at") {
            if (!p.parseString(out.last_seen_at, err)) return false;
        } else if (key == "heartbeat_source") {
            if (!p.parseString(out.heartbeat_source, err)) return false;
        } else if (key == "online_status") {
            if (!p.parseString(out.online_status, err)) return false;
        } else if (key == "features") {
            sawFeatures = true;
            if (!p.expect('{', err)) return false;
            if (p.consume('}')) {
                err = "features object is empty";
                return false;
            }
            while (true) {
                std::string f;
                if (!p.parseString(f, err)) return false;
                if (!p.expect(':', err)) return false;

                if (f == "ADAS") {
                    if (!parseFeature(p, out.adas, f, err)) return false;
                } else if (f == "Navigation") {
                    if (!parseFeature(p, out.navigation, f, err)) return false;
                } else if (f == "Energy") {
                    if (!parseFeature(p, out.energy, f, err)) return false;
                } else {
                    p.skipValue(err);
                    if (!err.empty()) return false;
                }

                if (p.consume('}')) break;
                if (!p.expect(',', err)) return false;
                if (p.consume('}')) {
                    err = "trailing comma in features object";
                    return false;
                }
            }
        } else if (key == "ota") {
            sawOta = true;
            if (!parseOta(p, out.ota, err)) return false;
        } else if (key == "last_ack") {
            StateManager::AckRecord ack{};
            if (!parseAck(p, ack, err)) return false;
            out.last_ack = std::move(ack);
        } else if (key == "last_deploy_id") {
            if (!p.parseString(out.last_deploy_id, err)) return false;
        } else if (key == "last_deploy_result") {
            if (!p.parseString(out.last_deploy_result, err)) return false;
        } else if (key == "last_deploy_time") {
            if (!p.parseString(out.last_deploy_time, err)) return false;
        } else if (key == "last_deploy_feature") {
            if (!p.parseString(out.last_deploy_feature, err)) return false;
        } else if (key == "last_deploy_slot") {
            if (!p.parseString(out.last_deploy_slot, err)) return false;
        } else if (key == "last_deploy_version") {
            if (!p.parseString(out.last_deploy_version, err)) return false;
        } else if (key == "last_deploy_reason") {
            if (!p.parseString(out.last_deploy_reason, err)) return false;
        } else {
            p.skipValue(err);
            if (!err.empty()) return false;
        }

        if (p.consume(',')) continue;
        if (p.consume('}')) break;
    }

    if (!sawFeatures) {
        err = "missing top-level key: features";
        return false;
    }
    if (!sawOta) {
        err = "missing top-level key: ota";
        return false;
    }
    return true;
}

void writeFeature(std::ostream& out, const StateManager::FeatureState& f, const std::string& name, int ind) {
    (void)name;
    indent(out, ind); out << "{\n";
    indent(out, ind + 2); out << "\"active_slot\": " << quote(f.active_slot) << ",\n";
    indent(out, ind + 2); out << "\"active_version\": " << quote(f.active_version) << ",\n";
    indent(out, ind + 2); out << "\"primary_slot\": " << quote(f.primary_slot.empty() ? f.active_slot : f.primary_slot) << ",\n";
    indent(out, ind + 2); out << "\"primary_version\": " << quote(f.primary_version.empty() ? f.active_version : f.primary_version) << ",\n";
    if (!f.primary_image.empty()) {
        indent(out, ind + 2); out << "\"primary_image\": " << quote(f.primary_image) << ",\n";
    }
    if (!f.artifact_type.empty()) {
        indent(out, ind + 2); out << "\"artifact_type\": " << quote(f.artifact_type) << ",\n";
    }
    if (!f.image_digest.empty()) {
        indent(out, ind + 2); out << "\"image_digest\": " << quote(f.image_digest) << ",\n";
    }
    if (!f.manifest_path.empty()) {
        indent(out, ind + 2); out << "\"manifest_path\": " << quote(f.manifest_path) << ",\n";
    }
    if (!f.package_id.empty()) {
        indent(out, ind + 2); out << "\"package_id\": " << quote(f.package_id) << ",\n";
    }
    if (!f.docker_runtime_result.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_result\": " << quote(f.docker_runtime_result) << ",\n";
    }
    if (!f.docker_runtime_mode.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_mode\": " << quote(f.docker_runtime_mode) << ",\n";
    }
    if (!f.docker_runtime_action.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_action\": " << quote(f.docker_runtime_action) << ",\n";
    }
    if (!f.docker_runtime_time.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_time\": " << quote(f.docker_runtime_time) << ",\n";
    }
    if (!f.docker_runtime_reason.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_reason\": " << quote(f.docker_runtime_reason) << ",\n";
    }
    if (!f.docker_runtime_container.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_container\": " << quote(f.docker_runtime_container) << ",\n";
    }
    if (!f.docker_runtime_image.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_image\": " << quote(f.docker_runtime_image) << ",\n";
    }
    if (!f.docker_runtime_pull_policy.empty()) {
        indent(out, ind + 2); out << "\"docker_runtime_pull_policy\": " << quote(f.docker_runtime_pull_policy) << ",\n";
    }
    indent(out, ind + 2); out << "\"pending_active_slot\": " << quote(f.pending_active_slot) << ",\n";
    indent(out, ind + 2); out << "\"pending_active_version\": " << quote(f.pending_active_version) << ",\n";
    indent(out, ind + 2); out << "\"flag\": " << quote(f.flag) << ",\n";

    if (!f.realtime_route_flag.empty()) {
        indent(out, ind + 2); out << "\"realtime_route_flag\": " << quote(f.realtime_route_flag) << ",\n";
    }
    if (!f.advanced_range_flag.empty()) {
        indent(out, ind + 2); out << "\"advanced_range_flag\": " << quote(f.advanced_range_flag) << ",\n";
    }
    if (!f.charging_recommend_flag.empty()) {
        indent(out, ind + 2); out << "\"charging_recommend_flag\": " << quote(f.charging_recommend_flag) << ",\n";
    }

    indent(out, ind + 2); out << "\"shadow_enabled\": " << (f.shadow_enabled ? "true" : "false") << ",\n";
    indent(out, ind + 2); out << "\"shadow_slot\": " << quote(f.shadow_slot) << ",\n";
    indent(out, ind + 2); out << "\"shadow_version\": " << quote(f.shadow_version) << ",\n";
    if (!f.shadow_image.empty()) {
        indent(out, ind + 2); out << "\"shadow_image\": " << quote(f.shadow_image) << ",\n";
    }
    {
        const std::string st = !f.shadow_status.empty()
            ? f.shadow_status
            : (f.shadow_enabled ? "RUNNING" : "DISABLED");
        indent(out, ind + 2); out << "\"shadow_status\": " << quote(st) << ",\n";
    }
    if (!f.shadow_started_at.empty()) {
        indent(out, ind + 2); out << "\"shadow_started_at\": " << quote(f.shadow_started_at) << ",\n";
    }
    if (!f.shadow_validation_result.empty()) {
        indent(out, ind + 2); out << "\"shadow_validation_result\": " << quote(f.shadow_validation_result) << ",\n";
    }
    if (!f.shadow_failure_reason.empty()) {
        indent(out, ind + 2); out << "\"shadow_failure_reason\": " << quote(f.shadow_failure_reason) << ",\n";
    }
    indent(out, ind + 2); out << "\"shadow_mode\": " << quote(deriveShadowMode(f)) << ",\n";
    indent(out, ind + 2); out << "\"kill_switch\": " << (f.kill_switch ? "true" : "false") << ",\n";
    indent(out, ind + 2); out << "\"kill_reason\": " << quote(f.kill_reason) << ",\n";
    indent(out, ind + 2); out << "\"fallback_slot\": " << quote(f.fallback_slot) << ",\n";
    indent(out, ind + 2); out << "\"fallback_version\": " << quote(f.fallback_version);

    auto writeRollbackStack = [&](const std::vector<StateManager::RollbackStackEntry>& v) {
        out << ",\n";
        indent(out, ind + 2); out << "\"rollback_stack\": [\n";
        for (std::size_t i = 0; i < v.size(); ++i) {
            const auto& e = v[i];
            indent(out, ind + 4); out << "{\n";
            indent(out, ind + 6); out << "\"slot\": " << quote(e.slot) << ",\n";
            indent(out, ind + 6); out << "\"version\": " << quote(e.version) << ",\n";
            indent(out, ind + 6); out << "\"docker_image\": " << quote(e.docker_image) << ",\n";
            indent(out, ind + 6); out << "\"previous_role\": " << quote(e.previous_role) << ",\n";
            indent(out, ind + 6); out << "\"recorded_at\": " << quote(e.recorded_at) << ",\n";
            indent(out, ind + 6); out << "\"reason\": " << quote(e.reason) << ",\n";
            indent(out, ind + 6); out << "\"status\": " << quote(e.status) << "\n";
            indent(out, ind + 4); out << "}";
            if (i + 1 < v.size()) out << ",";
            out << "\n";
        }
        indent(out, ind + 2); out << "]";
    };

    auto writeBlocked = [&](const char* nameKey, const std::vector<StateManager::BlockedVersionEntry>& v) {
        out << ",\n";
        indent(out, ind + 2); out << quote(nameKey) << ": [\n";
        for (std::size_t i = 0; i < v.size(); ++i) {
            const auto& e = v[i];
            indent(out, ind + 4); out << "{\n";
            indent(out, ind + 6); out << "\"slot\": " << quote(e.slot) << ",\n";
            indent(out, ind + 6); out << "\"version\": " << quote(e.version) << ",\n";
            indent(out, ind + 6); out << "\"docker_image\": " << quote(e.docker_image) << ",\n";
            indent(out, ind + 6); out << "\"blocked_at\": " << quote(e.blocked_at) << ",\n";
            indent(out, ind + 6); out << "\"reason\": " << quote(e.reason) << ",\n";
            indent(out, ind + 6); out << "\"source\": " << quote(e.source) << "\n";
            indent(out, ind + 4); out << "}";
            if (i + 1 < v.size()) out << ",";
            out << "\n";
        }
        indent(out, ind + 2); out << "]";
    };

    const bool anyRollbackFields = (!f.rollback_stack.empty() || !f.blocked_versions.empty() || !f.quarantine.empty());
    if (!anyRollbackFields) {
        out << "\n";
        indent(out, ind); out << "}";
        return;
    }

    if (!f.rollback_stack.empty()) {
        writeRollbackStack(f.rollback_stack);
    }
    if (!f.blocked_versions.empty()) {
        writeBlocked("blocked_versions", f.blocked_versions);
    }
    if (!f.quarantine.empty()) {
        writeBlocked("quarantine", f.quarantine);
    }

    out << "\n";
    indent(out, ind); out << "}";
}

void writeOta(std::ostream& out, const StateManager::OtaState& o, int ind) {
    indent(out, ind); out << "{\n";
    indent(out, ind + 2); out << "\"download_status\": " << quote(o.download_status) << ",\n";
    indent(out, ind + 2); out << "\"install_status\": " << quote(o.install_status) << ",\n";
    indent(out, ind + 2); out << "\"vehicle_mode\": " << quote(o.vehicle_mode) << ",\n";
    indent(out, ind + 2); out << "\"ignition\": " << quote(o.ignition) << ",\n";
    indent(out, ind + 2); out << "\"update_mode\": " << quote(o.update_mode) << ",\n";
    if (!o.ota_phase.empty()) {
        indent(out, ind + 2); out << "\"ota_phase\": " << quote(o.ota_phase) << ",\n";
    }
    if (!o.failure_reason.empty()) {
        indent(out, ind + 2); out << "\"failure_reason\": " << quote(o.failure_reason) << ",\n";
    }
    indent(out, ind + 2); out << "\"gear\": " << quote(o.gear) << ",\n";
    indent(out, ind + 2); out << "\"speed\": " << o.speed << ",\n";
    indent(out, ind + 2); out << "\"parking_brake\": " << quote(o.parking_brake) << ",\n";
    indent(out, ind + 2); out << "\"battery\": " << o.battery << ",\n";
    indent(out, ind + 2); out << "\"driver_approved\": " << (o.driver_approved ? "true" : "false") << ",\n";
    indent(out, ind + 2); out << "\"network\": " << quote(o.network) << ",\n";
    indent(out, ind + 2); out << "\"target_system_running\": " << (o.target_system_running ? "true" : "false") << "\n";
    indent(out, ind); out << "}";
}

void writeAck(std::ostream& out, const StateManager::AckRecord& a, int ind) {
    indent(out, ind); out << "{\n";
    indent(out, ind + 2); out << "\"status\": " << quote(a.status) << ",\n";
    indent(out, ind + 2); out << "\"at\": " << quote(a.at) << ",\n";
    indent(out, ind + 2); out << "\"feature\": " << quote(a.feature) << ",\n";
    indent(out, ind + 2); out << "\"version\": " << quote(a.version) << ",\n";
    indent(out, ind + 2); out << "\"deploy_id\": " << quote(a.deploy_id) << ",\n";
    indent(out, ind + 2); out << "\"message\": " << quote(a.message) << "\n";
    indent(out, ind); out << "}";
}

std::string writeVehicleStateJson(const StateManager::VehicleState& s) {
    std::ostringstream out;
    out << "{\n";
    indent(out, 2); out << "\"car_id\": " << quote(s.car_id) << ",\n";
    indent(out, 2); out << "\"model\": " << quote(s.model) << ",\n";
    indent(out, 2); out << "\"region\": " << quote(s.region) << ",\n";
    indent(out, 2); out << "\"group\": " << quote(s.group) << ",\n";
    indent(out, 2); out << "\"status\": " << quote(s.status);
    if (!s.last_seen_at.empty()) {
        out << ",\n";
        indent(out, 2); out << "\"last_seen_at\": " << quote(s.last_seen_at);
    }
    if (!s.heartbeat_source.empty()) {
        out << ",\n";
        indent(out, 2); out << "\"heartbeat_source\": " << quote(s.heartbeat_source);
    }
    if (!s.online_status.empty()) {
        out << ",\n";
        indent(out, 2); out << "\"online_status\": " << quote(s.online_status);
    }
    out << ",\n";

    indent(out, 2); out << "\"features\": {\n";
    indent(out, 4); out << "\"ADAS\": ";
    writeFeature(out, s.adas, "ADAS", 4);
    out << ",\n";
    indent(out, 4); out << "\"Navigation\": ";
    writeFeature(out, s.navigation, "Navigation", 4);
    out << ",\n";
    indent(out, 4); out << "\"Energy\": ";
    writeFeature(out, s.energy, "Energy", 4);
    out << "\n";
    indent(out, 2); out << "},\n";

    indent(out, 2); out << "\"ota\": ";
    writeOta(out, s.ota, 2);
    if (s.last_ack) {
        out << ",\n";
        indent(out, 2); out << "\"last_ack\": ";
        writeAck(out, *s.last_ack, 2);
    }
    if (!s.last_deploy_result.empty()) {
        out << ",\n";
        indent(out, 2); out << "\"last_deploy_id\": " << quote(s.last_deploy_id) << ",\n";
        indent(out, 2); out << "\"last_deploy_result\": " << quote(s.last_deploy_result) << ",\n";
        indent(out, 2); out << "\"last_deploy_time\": " << quote(s.last_deploy_time) << ",\n";
        indent(out, 2); out << "\"last_deploy_feature\": " << quote(s.last_deploy_feature) << ",\n";
        indent(out, 2); out << "\"last_deploy_slot\": " << quote(s.last_deploy_slot) << ",\n";
        indent(out, 2); out << "\"last_deploy_version\": " << quote(s.last_deploy_version) << ",\n";
        indent(out, 2); out << "\"last_deploy_reason\": " << quote(s.last_deploy_reason);
    }
    out << "\n";

    out << "}\n";
    return out.str();
}

std::string fleetDeployTimestampLocal() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

std::string makeFleetDeployId(const std::string& car_id, const std::string& feature) {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << "deploy_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << car_id << "_" << feature;
    return os.str();
}

}  // namespace

StateManager::StateManager(std::string workingDirectory)
    : workDir_(std::move(workingDirectory))
    , stateDir_(workDir_ + "/deploy/state") {}

std::string StateManager::statePath(const std::string& car_id) const {
    return stateDir_ + "/" + car_id + ".json";
}

bool StateManager::isValidCarId(const std::string& car_id) {
    return car_id.rfind("car_", 0) == 0 && car_id.size() >= 5;
}

bool StateManager::isValidFeature(const std::string& feature) {
    return feature == "ADAS" || feature == "Navigation" || feature == "Energy";
}

bool StateManager::isValidSlot(const std::string& slot, bool allowEmpty) {
    if (allowEmpty && slot.empty()) return true;
    return slot == "A" || slot == "B";
}

bool StateManager::isValidFlag(const std::string& flag) {
    return flag == "ON" || flag == "OFF";
}

bool StateManager::isValidDownloadStatus(const std::string& v) {
    return v == "idle" || v == "downloading" || v == "downloaded" || v == "failed";
}

bool StateManager::isValidInstallStatus(const std::string& v) {
    return v == "not_started" || v == "installing" || v == "installed" || v == "failed";
}

bool StateManager::isValidUpdateMode(const std::string& v) {
    return v == "READY" || v == "DOWNLOADING" || v == "READY_TO_INSTALL" || v == "INSTALLING" ||
        v == "COMPLETED" || v == "FAILED" || v == "ROLLBACK_READY";
}

bool StateManager::isValidOtaPhase(const std::string& v) {
    return v == "IDLE" ||
        v == "DOWNLOAD_STARTED" ||
        v == "DOWNLOAD_COMPLETED" ||
        v == "VERIFYING" ||
        v == "VERIFIED" ||
        v == "READY_TO_INSTALL" ||
        v == "INSTALL_STARTED" ||
        v == "INSTALL_COMPLETED" ||
        v == "INSTALL_FAILED" ||
        v == "DOWNLOAD_FAILED" ||
        v == "CHECKSUM_FAILED";
}

bool StateManager::isValidAckStatus(const std::string& v) {
    return v == "DOWNLOAD_STARTED" ||
        v == "DOWNLOAD_COMPLETED" ||
        v == "CHECKSUM_OK" ||
        v == "CHECKSUM_FAILED" ||
        v == "INSTALL_STARTED" ||
        v == "INSTALL_COMPLETED" ||
        v == "INSTALL_FAILED" ||
        v == "ROLLBACK_COMPLETED";
}

bool StateManager::isValidBlockedSource(const std::string& v) {
    return v == "shadow_validation_failed" || v == "primary_runtime_failed";
}

StateManager::FeatureState* StateManager::featureRef(VehicleState& s, const std::string& feature) {
    if (feature == "ADAS") return &s.adas;
    if (feature == "Navigation") return &s.navigation;
    if (feature == "Energy") return &s.energy;
    return nullptr;
}

const StateManager::FeatureState* StateManager::featureRef(const VehicleState& s, const std::string& feature) {
    if (feature == "ADAS") return &s.adas;
    if (feature == "Navigation") return &s.navigation;
    if (feature == "Energy") return &s.energy;
    return nullptr;
}

std::optional<StateManager::VehicleState> StateManager::loadState(const std::string& car_id,
    std::string& error) const {
    error.clear();
    if (!isValidCarId(car_id)) {
        error = "StateManager: invalid car_id: " + car_id;
        return std::nullopt;
    }
    const std::string path = statePath(car_id);
    std::ifstream in(path);
    if (!in.is_open()) {
        error = "StateManager: failed to open " + path;
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    VehicleState s;
    std::string parseErr;
    if (!parseVehicleState(ss.str(), s, parseErr)) {
        error = "StateManager: parse error in " + path + ": " + parseErr;
        return std::nullopt;
    }
    return s;
}

bool StateManager::saveState(const std::string& car_id, const VehicleState& state, std::string& error) const {
    error.clear();
    if (car_id != state.car_id) {
        error = "StateManager: car_id mismatch (arg=" + car_id + ", state=" + state.car_id + ")";
        return false;
    }
    if (!isValidCarId(car_id)) {
        error = "StateManager: invalid car_id: " + car_id;
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(stateDir_, ec);
    if (ec) {
        error = "StateManager: failed to create directory: " + stateDir_ + " (" + ec.message() + ")";
        return false;
    }

    const std::string path = statePath(car_id);
    const std::string tmp = path + ".tmp";
    std::filesystem::remove(tmp, ec);

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) {
            error = "StateManager: failed to write " + tmp;
            return false;
        }
        out << writeVehicleStateJson(state);
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "StateManager: failed to replace " + path + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

bool StateManager::updateFeatureSlot(const std::string& car_id, const std::string& feature,
    const std::string& slot, const std::string& version, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (!isValidSlot(slot)) {
        error = "StateManager: invalid slot: " + slot + " (expected A|B)";
        return false;
    }
    if (version.empty()) {
        error = "StateManager: version is empty";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }
    fs->active_slot = slot;
    fs->active_version = version;
    fs->primary_slot = slot;
    fs->primary_version = version;
    return saveState(car_id, *s, error);
}

bool StateManager::applyFeaturePackageMetadata(const std::string& car_id, const std::string& feature,
    const FeaturePackageMetadata& metadata, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) {
        return false;
    }
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }

    if (!metadata.primary_image.empty()) {
        fs->primary_image = metadata.primary_image;
    }
    if (!metadata.artifact_type.empty()) {
        fs->artifact_type = metadata.artifact_type;
    }
    if (!metadata.image_digest.empty()) {
        fs->image_digest = metadata.image_digest;
    }
    if (!metadata.manifest_path.empty()) {
        fs->manifest_path = metadata.manifest_path;
    }
    if (!metadata.package_id.empty()) {
        fs->package_id = metadata.package_id;
    }

    return saveState(car_id, *s, error);
}

bool StateManager::stageCandidatePackage(const std::string& car_id, const std::string& feature,
    const std::string& candidate_version, const FeaturePackageMetadata& metadata, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature;
        return false;
    }
    if (candidate_version.empty()) {
        error = "StateManager: candidate version is empty";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) {
        return false;
    }
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }

    const std::string cv = formatFleetVersionTag(candidate_version);
    fs->shadow_version = cv;
    fs->shadow_enabled = true;
    if (fs->shadow_slot.empty()) {
        fs->shadow_slot = "B";
    }
    fs->shadow_status = "RUNNING";
    fs->shadow_validation_result = "UNKNOWN";

    if (!metadata.primary_image.empty()) {
        fs->shadow_image = metadata.primary_image;
    }
    if (!metadata.artifact_type.empty()) {
        fs->artifact_type = metadata.artifact_type;
    }
    if (!metadata.image_digest.empty()) {
        fs->image_digest = metadata.image_digest;
    }
    if (!metadata.manifest_path.empty()) {
        fs->manifest_path = metadata.manifest_path;
    }
    if (!metadata.package_id.empty()) {
        fs->package_id = metadata.package_id;
    }

    return saveState(car_id, *s, error);
}

bool StateManager::reconcileFeatureWithVehicleRuntime(const std::string& car_id, const std::string& feature,
    bool after_promote, const std::string& docker_image_ref, const std::string& fleet_target_version,
    const std::string& preserved_primary_slot, const std::string& preserved_primary_version,
    std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature;
        return false;
    }

    const std::string domainKey = fleetFeatureToRuntimeDomainKey(feature);
    if (domainKey.empty()) {
        error = "StateManager: no vehicle_runtime domain mapping for feature: " + feature;
        return false;
    }

    const std::filesystem::path rtPath =
        std::filesystem::path(workDir_) / ".." / "deploy" / "vehicle_runtime" / (car_id + ".json");
    VehicleRuntimeDomainView rt{};
    if (!readVehicleRuntimeDomainFromFile(rtPath.generic_string(), domainKey, rt, error)) {
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) {
        return false;
    }
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }

    const std::string pkg_id = fs->package_id;
    const std::string manifest_path = fs->manifest_path;
    const std::string artifact_type = fs->artifact_type;
    const std::string image_digest = fs->image_digest;

    if (after_promote) {
        std::string pv = formatFleetVersionTag(rt.primary_version);
        if (pv.empty()) {
            pv = formatFleetVersionTag(fleet_target_version);
        }
        if (!pv.empty()) {
            fs->active_version = pv;
            fs->primary_version = pv;
        }
        if (!docker_image_ref.empty()) {
            fs->primary_image = docker_image_ref;
        }
        fs->shadow_version.clear();
        fs->shadow_image.clear();
        fs->shadow_enabled = false;
        fs->shadow_slot.clear();
        if (!fleet_target_version.empty()) {
            fs->active_slot = "B";
            fs->primary_slot = "B";
        }
    } else {
        std::string primaryVer = formatFleetVersionTag(rt.primary_version);
        if (primaryVer.empty()) {
            primaryVer = formatFleetVersionTag(preserved_primary_version);
        }
        std::string candidateVer = formatFleetVersionTag(rt.shadow_version);
        if (candidateVer.empty()) {
            candidateVer = formatFleetVersionTag(fleet_target_version);
        }

        if (!primaryVer.empty()) {
            fs->active_version = primaryVer;
            fs->primary_version = primaryVer;
        }
        if (!preserved_primary_slot.empty()) {
            fs->active_slot = preserved_primary_slot;
            fs->primary_slot = preserved_primary_slot;
        }
        if (!candidateVer.empty()) {
            fs->shadow_version = candidateVer;
        }
        fs->shadow_enabled = rt.shadow_enabled || !fs->shadow_version.empty();
        if (fs->shadow_enabled && fs->shadow_slot.empty()) {
            fs->shadow_slot = "B";
        }
        if (!docker_image_ref.empty()) {
            fs->shadow_image = docker_image_ref;
        }
        // primary_image keeps package metadata (candidate package ref) until promote.
    }

    if (!pkg_id.empty()) {
        fs->package_id = pkg_id;
    }
    if (!manifest_path.empty()) {
        fs->manifest_path = manifest_path;
    }
    if (!artifact_type.empty()) {
        fs->artifact_type = artifact_type;
    }
    if (!image_digest.empty()) {
        fs->image_digest = image_digest;
    }

    return saveState(car_id, *s, error);
}

bool StateManager::alignFeatureStateAfterDockerPromote(const std::string& car_id, const std::string& feature,
    const std::string& promoted_version, const std::string& promoted_image, std::string& error) const {
    return reconcileFeatureWithVehicleRuntime(car_id, feature, true, promoted_image, promoted_version, "", "", error);
}

bool StateManager::applyFeatureDockerRuntime(const std::string& car_id, const std::string& feature,
    const FeatureDockerRuntimeRecord& record, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) {
        return false;
    }
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }

    fs->docker_runtime_result = record.result;
    fs->docker_runtime_mode = record.mode;
    fs->docker_runtime_action = record.action;
    fs->docker_runtime_time = record.time;
    fs->docker_runtime_reason = record.reason;
    fs->docker_runtime_container = record.container;
    fs->docker_runtime_image = record.image;
    fs->docker_runtime_pull_policy = record.pull_policy;
    return saveState(car_id, *s, error);
}

bool StateManager::updateFeaturePending(const std::string& car_id, const std::string& feature,
    const std::string& slot, const std::string& version, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (!isValidSlot(slot)) {
        error = "StateManager: invalid slot: " + slot + " (expected A|B)";
        return false;
    }
    if (version.empty()) {
        error = "StateManager: version is empty";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }
    fs->pending_active_slot = slot;
    fs->pending_active_version = version;
    return saveState(car_id, *s, error);
}

bool StateManager::promotePendingToActive(const std::string& car_id, const std::string& feature,
    std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }
    if (fs->pending_active_version.empty()) {
        return true;
    }
    if (!isValidSlot(fs->pending_active_slot)) {
        error = "StateManager: invalid pending_active_slot: " + fs->pending_active_slot;
        return false;
    }
    fs->active_slot = fs->pending_active_slot;
    fs->active_version = fs->pending_active_version;
    fs->primary_slot = fs->active_slot;
    fs->primary_version = fs->active_version;
    fs->pending_active_slot.clear();
    fs->pending_active_version.clear();
    return saveState(car_id, *s, error);
}

bool StateManager::updateFeatureFlag(const std::string& car_id, const std::string& feature,
    const std::string& flag, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (!isValidFlag(flag)) {
        error = "StateManager: invalid flag: " + flag + " (expected ON|OFF)";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }
    fs->flag = flag;
    return saveState(car_id, *s, error);
}

bool StateManager::updateKillSwitch(const std::string& car_id, const std::string& feature,
    bool enabled, const std::string& reason, std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }
    fs->kill_switch = enabled;
    fs->kill_reason = enabled ? reason : std::string{};
    return saveState(car_id, *s, error);
}

bool StateManager::updateShadowState(const std::string& car_id, const std::string& feature,
    bool enabled, const std::string& shadow_slot, const std::string& shadow_version,
    std::string& error) const {
    error.clear();
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (!isValidSlot(shadow_slot, true)) {
        error = "StateManager: invalid shadow_slot: " + shadow_slot + " (expected A|B or empty)";
        return false;
    }
    if (enabled && (shadow_slot.empty() || shadow_version.empty())) {
        error = "StateManager: shadow enabled requires shadow_slot and shadow_version";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal error: featureRef failed";
        return false;
    }
    fs->shadow_enabled = enabled;
    fs->shadow_slot = enabled ? shadow_slot : std::string{};
    fs->shadow_version = enabled ? shadow_version : std::string{};
    if (enabled) {
        if (fs->shadow_status.empty() || fs->shadow_status == "DISABLED") {
            fs->shadow_status = "RUNNING";
        }
        // Record the "shadow ON" time here (event-time), not during JSON parsing.
        fs->shadow_started_at = nowUtcIsoZ();
        if (fs->shadow_validation_result.empty()) {
            fs->shadow_validation_result = "UNKNOWN";
        }
        // Do not overwrite existing failure_reason/result when enabling; validation flow can update later.
    } else {
        fs->shadow_status = "DISABLED";
        // Policy: clear timestamps for accuracy (this is not a history log).
        // If we later want a history trail, it should go to deploy/history/ instead.
        fs->shadow_started_at.clear();
        // Keep validation fields for post-mortem unless explicitly cleared by future commands.
    }
    return saveState(car_id, *s, error);
}

bool StateManager::updateOtaProgressForDeploy(const std::string& car_id,
    const std::string* download_status,
    const std::string* install_status,
    const std::string* update_mode,
    const bool* target_system_running,
    std::string& error) const {
    error.clear();

    auto s = loadState(car_id, error);
    if (!s) return false;

    if (download_status) {
        if (!isValidDownloadStatus(*download_status)) {
            error = "StateManager: invalid download_status: " + *download_status;
            return false;
        }
        s->ota.download_status = *download_status;
    }
    if (install_status) {
        if (!isValidInstallStatus(*install_status)) {
            error = "StateManager: invalid install_status: " + *install_status;
            return false;
        }
        s->ota.install_status = *install_status;
    }
    if (update_mode) {
        if (!isValidUpdateMode(*update_mode)) {
            error = "StateManager: invalid update_mode: " + *update_mode;
            return false;
        }
        s->ota.update_mode = *update_mode;
    }
    if (target_system_running) {
        s->ota.target_system_running = *target_system_running;
    }

    return saveState(car_id, *s, error);
}

bool StateManager::updateOtaDownloadStatus(const std::string& car_id, const std::string& download_status,
    std::string& error) const {
    return updateOtaProgressForDeploy(car_id, &download_status, nullptr, nullptr, nullptr, error);
}

bool StateManager::updateOtaInstallStatus(const std::string& car_id, const std::string& install_status,
    std::string& error) const {
    return updateOtaProgressForDeploy(car_id, nullptr, &install_status, nullptr, nullptr, error);
}

bool StateManager::updateOtaUpdateMode(const std::string& car_id, const std::string& update_mode,
    std::string& error) const {
    return updateOtaProgressForDeploy(car_id, nullptr, nullptr, &update_mode, nullptr, error);
}

bool StateManager::updateDriverApproval(const std::string& car_id, bool approved, std::string& error) const {
    error.clear();
    auto s = loadState(car_id, error);
    if (!s) return false;
    s->ota.driver_approved = approved;
    return saveState(car_id, *s, error);
}

bool StateManager::updateTargetSystemRunning(const std::string& car_id, bool running, std::string& error) const {
    error.clear();
    auto s = loadState(car_id, error);
    if (!s) return false;
    s->ota.target_system_running = running;
    return saveState(car_id, *s, error);
}

bool StateManager::updateOtaPhase(const std::string& car_id,
    const std::string& ota_phase,
    const std::string& failure_reason,
    std::string& error) const {
    error.clear();
    if (!isValidOtaPhase(ota_phase)) {
        error = "StateManager: invalid ota_phase: " + ota_phase;
        return false;
    }
    auto s = loadState(car_id, error);
    if (!s) return false;
    s->ota.ota_phase = ota_phase;
    s->ota.failure_reason = failure_reason;
    return saveState(car_id, *s, error);
}

bool StateManager::recordLastDeploy(const std::string& car_id,
    const std::string& feature,
    const std::string& slot,
    const std::string& version,
    const std::string& result,
    const std::string& reason,
    std::string& error) const {
    error.clear();
    if (!isValidCarId(car_id)) {
        error = "StateManager: invalid car_id: " + car_id;
        return false;
    }
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature;
        return false;
    }
    if (result != "success" && result != "failed" && result != "pending") {
        error = "StateManager: invalid last_deploy result: " + result;
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) {
        return false;
    }

    s->last_deploy_id = makeFleetDeployId(car_id, feature);
    s->last_deploy_result = result;
    s->last_deploy_time = fleetDeployTimestampLocal();
    s->last_deploy_feature = feature;
    s->last_deploy_slot = slot;
    s->last_deploy_version = version;
    s->last_deploy_reason = reason;
    return saveState(car_id, *s, error);
}

bool StateManager::recordAck(const std::string& car_id,
    const std::string& feature,
    const std::string& ack_status,
    const std::string& message,
    const std::string& version,
    const std::string& deploy_id,
    std::string& error) const {
    error.clear();
    if (!isValidCarId(car_id)) {
        error = "StateManager: invalid car_id: " + car_id;
        return false;
    }
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (!isValidAckStatus(ack_status)) {
        error =
            "StateManager: invalid ack status: " + ack_status +
            " (expected DOWNLOAD_STARTED|DOWNLOAD_COMPLETED|CHECKSUM_OK|CHECKSUM_FAILED|INSTALL_STARTED|INSTALL_COMPLETED|INSTALL_FAILED|ROLLBACK_COMPLETED)";
        return false;
    }
    if (message.empty()) {
        error = "StateManager: ack message is empty";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;

    AckRecord r{};
    r.status = ack_status;
    r.at = nowUtcIsoZ();
    r.message = message;
    r.feature = feature;
    r.version = version;
    r.deploy_id = deploy_id;

    s->last_ack = std::move(r);
    // In this step, ACK acts as a file-based "vehicle response" signal. Treat it as last-seen.
    // Use the same timestamp as the ACK record so tooling can correlate them.
    s->last_seen_at = s->last_ack ? s->last_ack->at : nowUtcIsoZ();
    s->heartbeat_source = "ack";
    return saveState(car_id, *s, error);
}

bool StateManager::pushRollbackStackEntry(const std::string& car_id,
    const std::string& feature,
    const RollbackStackEntry& entry,
    std::size_t maxDepth,
    std::string& error) const {
    error.clear();
    if (!isValidCarId(car_id)) {
        error = "StateManager: invalid car_id: " + car_id;
        return false;
    }
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (entry.version.empty()) {
        error = "StateManager: rollback_stack entry missing version";
        return false;
    }
    if (maxDepth == 0) return true;  // no-op by policy

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal unknown feature: " + feature;
        return false;
    }

    // Remove duplicates (same version + docker_image), then push most-recent-first.
    fs->rollback_stack.erase(
        std::remove_if(fs->rollback_stack.begin(), fs->rollback_stack.end(),
            [&](const RollbackStackEntry& e) { return e.version == entry.version && e.docker_image == entry.docker_image; }),
        fs->rollback_stack.end());
    fs->rollback_stack.insert(fs->rollback_stack.begin(), entry);

    if (fs->rollback_stack.size() > maxDepth) {
        fs->rollback_stack.resize(maxDepth);
    }
    return saveState(car_id, *s, error);
}

bool StateManager::addBlockedVersion(const std::string& car_id,
    const std::string& feature,
    const BlockedVersionEntry& entry,
    std::string& error) const {
    error.clear();
    if (!isValidCarId(car_id)) {
        error = "StateManager: invalid car_id: " + car_id;
        return false;
    }
    if (!isValidFeature(feature)) {
        error = "StateManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return false;
    }
    if (entry.version.empty()) {
        error = "StateManager: blocked_versions entry missing version";
        return false;
    }
    if (!entry.source.empty() && !isValidBlockedSource(entry.source)) {
        error = "StateManager: invalid blocked source: " + entry.source +
            " (expected shadow_validation_failed|primary_runtime_failed)";
        return false;
    }

    auto s = loadState(car_id, error);
    if (!s) return false;
    auto* fs = featureRef(*s, feature);
    if (!fs) {
        error = "StateManager: internal unknown feature: " + feature;
        return false;
    }

    // Best-effort remove blocked version from rollback_stack to avoid re-selection.
    fs->rollback_stack.erase(
        std::remove_if(fs->rollback_stack.begin(), fs->rollback_stack.end(),
            [&](const RollbackStackEntry& e) { return e.version == entry.version; }),
        fs->rollback_stack.end());

    fs->blocked_versions.erase(
        std::remove_if(fs->blocked_versions.begin(), fs->blocked_versions.end(),
            [&](const BlockedVersionEntry& e) { return e.version == entry.version && e.docker_image == entry.docker_image; }),
        fs->blocked_versions.end());
    fs->blocked_versions.insert(fs->blocked_versions.begin(), entry);

    return saveState(car_id, *s, error);
}

std::string StateManager::selfTest() const {
    std::string err;
    const auto s1 = loadState("car_1", err);
    if (!s1) return err;
    if (s1->car_id != "car_1") return "selfTest: car_id mismatch";
    if (s1->adas.active_slot != "A" || s1->adas.active_version != "v1.0") return "selfTest: ADAS A mismatch";
    if (s1->navigation.active_slot != "A" || s1->navigation.active_version != "v1.0") return "selfTest: NAV A mismatch";
    if (s1->energy.active_slot != "A" || s1->energy.active_version != "v1.0") return "selfTest: Energy A mismatch";
    if (s1->ota.speed != 0) return "selfTest: ota.speed mismatch";
    return {};
}

}  // namespace sdv::platform::deployment

