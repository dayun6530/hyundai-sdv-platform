#include "DeploymentManager/StateManager.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

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
        if (c == '{') {
            int depth = 0;
            while (!eof()) {
                char x = s_[i_++];
                if (x == '"') {
                    --i_;
                    std::string tmp;
                    if (!parseString(tmp, err)) return;
                    continue;
                }
                if (x == '{') ++depth;
                if (x == '}') {
                    if (depth == 0) break;
                    --depth;
                }
            }
            return;
        }
        if (c == '[') {
            int depth = 0;
            while (!eof()) {
                char x = s_[i_++];
                if (x == '"') {
                    --i_;
                    std::string tmp;
                    if (!parseString(tmp, err)) return;
                    continue;
                }
                if (x == '[') ++depth;
                if (x == ']') {
                    if (depth == 0) break;
                    --depth;
                }
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

        if (key == "active_slot") {
            if (!p.parseString(out.active_slot, err)) return false;
        } else if (key == "active_version") {
            if (!p.parseString(out.active_version, err)) return false;
        } else if (key == "flag") {
            if (!p.parseString(out.flag, err)) return false;
        } else if (key == "shadow_enabled") {
            if (!p.parseBool(out.shadow_enabled, err)) return false;
        } else if (key == "shadow_slot") {
            if (!p.parseString(out.shadow_slot, err)) return false;
        } else if (key == "shadow_version") {
            if (!p.parseString(out.shadow_version, err)) return false;
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
    indent(out, ind + 2); out << "\"kill_switch\": " << (f.kill_switch ? "true" : "false") << ",\n";
    indent(out, ind + 2); out << "\"kill_reason\": " << quote(f.kill_reason) << ",\n";
    indent(out, ind + 2); out << "\"fallback_slot\": " << quote(f.fallback_slot) << ",\n";
    indent(out, ind + 2); out << "\"fallback_version\": " << quote(f.fallback_version) << "\n";
    indent(out, ind); out << "}";
}

void writeOta(std::ostream& out, const StateManager::OtaState& o, int ind) {
    indent(out, ind); out << "{\n";
    indent(out, ind + 2); out << "\"download_status\": " << quote(o.download_status) << ",\n";
    indent(out, ind + 2); out << "\"install_status\": " << quote(o.install_status) << ",\n";
    indent(out, ind + 2); out << "\"vehicle_mode\": " << quote(o.vehicle_mode) << ",\n";
    indent(out, ind + 2); out << "\"ignition\": " << quote(o.ignition) << ",\n";
    indent(out, ind + 2); out << "\"update_mode\": " << quote(o.update_mode) << ",\n";
    indent(out, ind + 2); out << "\"gear\": " << quote(o.gear) << ",\n";
    indent(out, ind + 2); out << "\"speed\": " << o.speed << ",\n";
    indent(out, ind + 2); out << "\"parking_brake\": " << quote(o.parking_brake) << ",\n";
    indent(out, ind + 2); out << "\"battery\": " << o.battery << ",\n";
    indent(out, ind + 2); out << "\"driver_approved\": " << (o.driver_approved ? "true" : "false") << ",\n";
    indent(out, ind + 2); out << "\"network\": " << quote(o.network) << ",\n";
    indent(out, ind + 2); out << "\"target_system_running\": " << (o.target_system_running ? "true" : "false") << "\n";
    indent(out, ind); out << "}";
}

std::string writeVehicleStateJson(const StateManager::VehicleState& s) {
    std::ostringstream out;
    out << "{\n";
    indent(out, 2); out << "\"car_id\": " << quote(s.car_id) << ",\n";
    indent(out, 2); out << "\"model\": " << quote(s.model) << ",\n";
    indent(out, 2); out << "\"region\": " << quote(s.region) << ",\n";
    indent(out, 2); out << "\"group\": " << quote(s.group) << ",\n";
    indent(out, 2); out << "\"status\": " << quote(s.status) << ",\n";

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
    out << "\n";

    out << "}\n";
    return out.str();
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

std::string StateManager::selfTest() const {
    std::string err;
    const auto s1 = loadState("car_1", err);
    if (!s1) return err;
    if (s1->car_id != "car_1") return "selfTest: car_id mismatch";
    if (s1->adas.active_slot != "A" || s1->adas.active_version != "v0.1") return "selfTest: ADAS A mismatch";
    if (s1->navigation.active_slot != "A" || s1->navigation.active_version != "v1.0") return "selfTest: NAV A mismatch";
    if (s1->energy.active_slot != "A" || s1->energy.active_version != "v0.5") return "selfTest: Energy A mismatch";
    if (s1->ota.speed != 0) return "selfTest: ota.speed mismatch";
    return {};
}

}  // namespace sdv::platform::deployment

