#include "DeploymentManager/VehicleRegistry.h"

#include <cctype>
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
            // NOTE: vehicles.json is ASCII-only in our use; keep it simple (no \uXXXX).
            r.push_back(c);
        }
        err = "unterminated string";
        return false;
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
            std::size_t depth = 0;
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
            std::size_t depth = 0;
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
            if (std::isspace(static_cast<unsigned char>(x)) || x == ',' || x == ']' || x == '}') {
                break;
            }
            ++i_;
        }
    }

private:
    void skipWs() {
        while (!eof() && std::isspace(static_cast<unsigned char>(s_[i_]))) {
            ++i_;
        }
    }

    std::string_view s_;
    std::size_t i_{0};
};

bool parseVehiclesArray(std::string_view json, std::vector<VehicleRegistry::Vehicle>& out, std::string& err) {
    JsonLite p(json);

    if (!p.expect('{', err)) return false;

    bool sawVehicles = false;
    while (true) {
        // end object
        if (p.consume('}')) break;

        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        if (key == "vehicles") {
            sawVehicles = true;
            if (!p.expect('[', err)) return false;

            // Standard JSON: [ obj (, obj)* ]
            if (p.consume(']')) {
                // empty array ok
            } else {
                while (true) {
                    if (!p.expect('{', err)) return false;

                    VehicleRegistry::Vehicle v{};
                    if (!p.consume('}')) {
                        while (true) {
                            std::string fkey;
                            if (!p.parseString(fkey, err)) return false;
                            if (!p.expect(':', err)) return false;

                            std::string sval;
                            if (!p.parseString(sval, err)) return false;

                            if (fkey == "car_id") v.car_id = std::move(sval);
                            else if (fkey == "model") v.model = std::move(sval);
                            else if (fkey == "region") v.region = std::move(sval);
                            else if (fkey == "group") v.group = std::move(sval);
                            else if (fkey == "status") v.status = std::move(sval);
                            // ignore unknown fields

                            if (p.consume('}')) break;
                            if (!p.expect(',', err)) return false;
                        }
                    }

                    out.push_back(std::move(v));

                    if (p.consume(']')) break;
                    if (!p.expect(',', err)) return false;
                    if (p.consume(']')) {
                        err = "trailing comma in vehicles array";
                        return false;
                    }
                }
            }
        } else {
            // ignore other keys (schema_version, etc.)
            p.skipValue(err);
            if (!err.empty()) return false;
        }

        if (p.consume(',')) continue;
        if (p.consume('}')) break;
    }

    if (!sawVehicles) {
        err = "missing top-level key: vehicles";
        return false;
    }
    return true;
}

}  // namespace

std::optional<VehicleRegistry> VehicleRegistry::loadFromFile(const std::string& path, std::string& error) {
    error.clear();

    std::ifstream in(path);
    if (!in.is_open()) {
        error = "VehicleRegistry: failed to open " + path;
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    VehicleRegistry reg;
    std::string parseErr;
    if (!parseVehiclesArray(ss.str(), reg.vehicles_, parseErr)) {
        error = "VehicleRegistry: parse error in " + path + ": " + parseErr;
        return std::nullopt;
    }

    for (const auto& v : reg.vehicles_) {
        std::string vErr;
        if (!validateVehicle(v, vErr)) {
            error = "VehicleRegistry: invalid vehicle in " + path + ": " + vErr;
            return std::nullopt;
        }
    }

    reg.index();
    return reg;
}

std::optional<VehicleRegistry::Vehicle> VehicleRegistry::getByCarId(const std::string& car_id) const {
    const auto it = byCarId_.find(car_id);
    if (it == byCarId_.end()) {
        return std::nullopt;
    }
    return vehicles_[it->second];
}

std::vector<VehicleRegistry::Vehicle> VehicleRegistry::findByGroup(const std::string& group) const {
    std::vector<Vehicle> r;
    for (const auto& v : vehicles_) {
        if (v.group == group) r.push_back(v);
    }
    return r;
}

std::vector<VehicleRegistry::Vehicle> VehicleRegistry::findByRegion(const std::string& region) const {
    std::vector<Vehicle> r;
    for (const auto& v : vehicles_) {
        if (v.region == region) r.push_back(v);
    }
    return r;
}

std::vector<VehicleRegistry::Vehicle> VehicleRegistry::findByModel(const std::string& model) const {
    std::vector<Vehicle> r;
    for (const auto& v : vehicles_) {
        if (v.model == model) r.push_back(v);
    }
    return r;
}

std::vector<VehicleRegistry::Vehicle> VehicleRegistry::findByGroupAndRegion(const std::string& group,
    const std::string& region) const {
    std::vector<Vehicle> r;
    for (const auto& v : vehicles_) {
        if (v.group == group && v.region == region) r.push_back(v);
    }
    return r;
}

std::string VehicleRegistry::selfTest() const {
    // Minimal invariants useful for smoke-checking sample dataset.
    if (vehicles_.empty()) return "no vehicles loaded";
    if (byCarId_.size() != vehicles_.size()) return "duplicate car_id detected";

    // Example: canary + KR should typically exist in sample.
    const auto canaryKr = findByGroupAndRegion("canary", "KR");
    if (canaryKr.empty()) return "canary+KR returned 0 vehicles (unexpected for sample)";
    return {};
}

void VehicleRegistry::index() {
    byCarId_.clear();
    for (std::size_t i = 0; i < vehicles_.size(); ++i) {
        byCarId_[vehicles_[i].car_id] = i;
    }
}

bool VehicleRegistry::validateVehicle(const Vehicle& v, std::string& error) {
    if (v.car_id.empty()) {
        error = "missing car_id";
        return false;
    }
    if (v.model.empty()) {
        error = "missing model for " + v.car_id;
        return false;
    }
    if (v.region.empty()) {
        error = "missing region for " + v.car_id;
        return false;
    }
    if (v.group.empty()) {
        error = "missing group for " + v.car_id;
        return false;
    }
    if (v.status.empty()) {
        error = "missing status for " + v.car_id;
        return false;
    }
    return true;
}

}  // namespace sdv::platform::deployment

