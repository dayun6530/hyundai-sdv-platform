#include "DeploymentManager/VersionManager.h"

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

    std::string_view s_;
    std::size_t i_{0};
};

bool parseSlotObject(JsonLite& p, VersionManager::SlotData& out, std::string& err) {
    if (!p.expect('{', err)) return false;
    if (p.consume('}')) {
        err = "slot object is empty";
        return false;
    }

    bool haveVersion = false;
    bool haveRole = false;
    bool haveBinary = false;

    while (true) {
        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        std::string val;
        if (!p.parseString(val, err)) return false;

        if (key == "version") {
            out.version = std::move(val);
            haveVersion = true;
        } else if (key == "role") {
            out.role = std::move(val);
            haveRole = true;
        } else if (key == "binary") {
            out.binary = std::move(val);
            haveBinary = true;
        } else if (key == "package_id") {
            out.package_id = std::move(val);
        } else if (key == "package_path") {
            out.package_path = std::move(val);
        } else if (key == "docker_image") {
            out.docker_image = std::move(val);
        } else if (key == "artifact_type") {
            out.artifact_type = std::move(val);
        } else {
            // ignore unknown string fields
        }

        if (p.consume('}')) break;
        if (!p.expect(',', err)) return false;
        if (p.consume('}')) {
            err = "trailing comma in slot object";
            return false;
        }
    }

    if (!haveVersion || !haveRole || !haveBinary) {
        err = "slot missing required fields (version/role/binary)";
        return false;
    }
    return true;
}

bool parseFeatureSlots(JsonLite& p, VersionManager::FeatureData& out, std::string& err) {
    if (!p.expect('{', err)) return false;
    if (p.consume('}')) {
        err = "feature object is empty";
        return false;
    }

    while (true) {
        std::string slotKey;
        if (!p.parseString(slotKey, err)) return false;
        if (!p.expect(':', err)) return false;

        if (slotKey == "A") {
            VersionManager::SlotData a{};
            if (!parseSlotObject(p, a, err)) return false;
            out.a = std::move(a);
        } else if (slotKey == "B") {
            VersionManager::SlotData b{};
            if (!parseSlotObject(p, b, err)) return false;
            out.b = std::move(b);
        } else {
            // unknown slot key: skip value
            p.skipValue(err);
            if (!err.empty()) return false;
        }

        if (p.consume('}')) break;
        if (!p.expect(',', err)) return false;
        if (p.consume('}')) {
            err = "trailing comma in feature object";
            return false;
        }
    }

    return true;
}

bool parseVersionMap(std::string_view json, std::unordered_map<std::string, VersionManager::FeatureData>& out,
    std::string& err) {
    JsonLite p(json);
    if (!p.expect('{', err)) return false;

    bool sawFeatures = false;
    while (true) {
        if (p.consume('}')) break;

        std::string key;
        if (!p.parseString(key, err)) return false;
        if (!p.expect(':', err)) return false;

        if (key == "features") {
            sawFeatures = true;
            if (!p.expect('{', err)) return false;
            if (p.consume('}')) {
                err = "features object is empty";
                return false;
            }

            while (true) {
                std::string featureName;
                if (!p.parseString(featureName, err)) return false;
                if (!p.expect(':', err)) return false;

                VersionManager::FeatureData fd{};
                if (!parseFeatureSlots(p, fd, err)) return false;
                out[featureName] = std::move(fd);

                if (p.consume('}')) break;
                if (!p.expect(',', err)) return false;
                if (p.consume('}')) {
                    err = "trailing comma in features object";
                    return false;
                }
            }
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
    return true;
}

}  // namespace

bool VersionManager::isValidSlot(char slot) {
    return slot == 'A' || slot == 'B';
}

bool VersionManager::isSupportedFeatureName(const std::string& feature) {
    return feature == "ADAS" || feature == "Navigation" || feature == "Energy";
}

std::optional<VersionManager> VersionManager::loadFromFile(const std::string& path, std::string& error) {
    error.clear();

    std::ifstream in(path);
    if (!in.is_open()) {
        error = "VersionManager: failed to open " + path;
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    VersionManager vm;
    std::string parseErr;
    if (!parseVersionMap(ss.str(), vm.features_, parseErr)) {
        error = "VersionManager: parse error in " + path + ": " + parseErr;
        return std::nullopt;
    }

    // Validate required features and slot presence/fields.
    for (const std::string feature : {"ADAS", "Navigation", "Energy"}) {
        const auto it = vm.features_.find(feature);
        if (it == vm.features_.end()) {
            error = "VersionManager: missing feature: " + feature + " (in " + path + ")";
            return std::nullopt;
        }
        if (!it->second.a || !it->second.b) {
            error = "VersionManager: feature missing A/B slots: " + feature + " (in " + path + ")";
            return std::nullopt;
        }
        if (it->second.a->version.empty() || it->second.a->role.empty() || it->second.a->binary.empty()) {
            error = "VersionManager: ADAS A slot missing fields (in " + path + ")";
            return std::nullopt;
        }
        if (it->second.b->version.empty() || it->second.b->role.empty() || it->second.b->binary.empty()) {
            error = "VersionManager: ADAS B slot missing fields (in " + path + ")";
            return std::nullopt;
        }
    }

    return vm;
}

bool VersionManager::hasFeature(const std::string& feature) const {
    return features_.find(feature) != features_.end();
}

bool VersionManager::hasBothSlots(const std::string& feature) const {
    const auto it = features_.find(feature);
    if (it == features_.end()) return false;
    return it->second.a.has_value() && it->second.b.has_value();
}

std::optional<VersionManager::VersionInfo> VersionManager::getInfo(const std::string& feature, char slot,
    std::string& error) const {
    error.clear();

    if (!isSupportedFeatureName(feature)) {
        error = "VersionManager: unsupported feature: " + feature + " (expected ADAS|Navigation|Energy)";
        return std::nullopt;
    }
    if (!isValidSlot(slot)) {
        error = std::string("VersionManager: invalid slot: '") + slot + "' (expected 'A' or 'B')";
        return std::nullopt;
    }

    const auto it = features_.find(feature);
    if (it == features_.end()) {
        error = "VersionManager: missing feature: " + feature;
        return std::nullopt;
    }

    const SlotData* sd = nullptr;
    if (slot == 'A') {
        if (!it->second.a) {
            error = "VersionManager: missing slot A for feature: " + feature;
            return std::nullopt;
        }
        sd = &(*it->second.a);
    } else {
        if (!it->second.b) {
            error = "VersionManager: missing slot B for feature: " + feature;
            return std::nullopt;
        }
        sd = &(*it->second.b);
    }

    VersionInfo info;
    info.feature = feature;
    info.slot = slot;
    info.version = sd->version;
    info.role = sd->role;
    info.binary = sd->binary;
    info.package_id = sd->package_id;
    info.package_path = sd->package_path;
    info.docker_image = sd->docker_image;
    info.artifact_type = sd->artifact_type;
    return info;
}

std::optional<std::string> VersionManager::getBinary(const std::string& feature, char slot, std::string& error) const {
    const auto info = getInfo(feature, slot, error);
    if (!info) return std::nullopt;
    return info->binary;
}

std::optional<std::string> VersionManager::getRole(const std::string& feature, char slot, std::string& error) const {
    const auto info = getInfo(feature, slot, error);
    if (!info) return std::nullopt;
    return info->role;
}

std::string VersionManager::selfTest() const {
    std::string err;
    auto check = [&](const std::string& f, char s, const std::string& v, const std::string& role,
                     const std::string& bin) -> std::string {
        const auto info = getInfo(f, s, err);
        if (!info) return err;
        if (info->version != v || info->role != role || info->binary != bin) {
            return "VersionManager selfTest mismatch for " + f + " " + std::string(1, s);
        }
        return {};
    };

    if (const auto e = check("ADAS", 'A', "v0.1", "stable", "AdasFeature"); !e.empty()) return e;
    if (const auto e = check("ADAS", 'B', "v0.2", "candidate", "AdasFeature"); !e.empty()) return e;
    if (const auto e = check("Navigation", 'A', "v1.0", "stable", "NavigationFeature"); !e.empty()) return e;
    if (const auto e = check("Navigation", 'B', "v1.1", "candidate", "NavigationFeature"); !e.empty()) return e;
    if (const auto e = check("Energy", 'A', "v0.5", "stable", "EvEnergyFeature"); !e.empty()) return e;
    if (const auto e = check("Energy", 'B', "v0.6", "candidate", "EvEnergyFeature"); !e.empty()) return e;

    return {};
}

}  // namespace sdv::platform::deployment

