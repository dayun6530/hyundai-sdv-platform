#include "DeploymentManager/ConfigUpdater.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace sdv::platform::deployment {

namespace {

const std::unordered_set<std::string> kManagedFeatures = {
    "NavigationFeature",
    "NavigationFeatureShadow",
    "AdasFeature",
    "AdasFeatureShadow",
    "EvEnergyFeature",
    "EvEnergyFeatureShadow",
    "WatchDog",
    "FeatureEvaluator",
};

constexpr const char kFleetBlockStart[] =
    "# --- fleet deploy (managed by DeploymentMgr sync-config; do not edit) ---";
constexpr const char kFleetBlockEnd[] = "# --- end fleet deploy ---";

void emitFeatureBlock(std::ostream& out, const ConfigUpdater::Selection& s) {
    (void)s;
    out << "feature WatchDog\n";
    out << "feature NavigationFeature\n";
    out << "feature NavigationFeatureShadow\n";
    out << "feature AdasFeature\n";
    out << "feature AdasFeatureShadow\n";
    out << "feature EvEnergyFeature\n";
    out << "feature EvEnergyFeatureShadow\n";
    out << "feature FeatureEvaluator\n";
}

bool validateActiveSlot(const std::string& slot, const char* domainLabel, std::string& error) {
    if (slot.empty()) {
        error = std::string("invalid ") + domainLabel + R"( slot: "")";
        return false;
    }
    if (slot != "A" && slot != "B") {
        error = std::string("invalid ") + domainLabel + " slot: " + slot;
        return false;
    }
    return true;
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

bool isFleetBlockStartLine(const std::string& line) {
    return trimCopy(line) == kFleetBlockStart;
}

bool isFleetBlockEndLine(const std::string& line) {
    return trimCopy(line) == kFleetBlockEnd;
}

std::string normalizeVersionTag(std::string v) {
    v = trimCopy(std::move(v));
    if (v.empty()) {
        return v;
    }
    if (v[0] == 'v' || v[0] == 'V') {
        return std::string("v") + v.substr(1);
    }
    if (std::isdigit(static_cast<unsigned char>(v[0]))) {
        return std::string("v") + v;
    }
    return v;
}

std::string defaultFleetImage(const char* domainKey, const std::string& version) {
    const std::string ver = normalizeVersionTag(version);
    return std::string("sdv/") + domainKey + "-feature:" + ver;
}

std::string fleetEnabledToken(const StateManager::FeatureState& fs) {
    if (fs.kill_switch) {
        return "OFF";
    }
    return fs.flag == "ON" ? "ON" : "OFF";
}

void emitFleetDomainLines(std::ostream& out,
    const char* prefix,
    const char* domainImageKey,
    const char* binaryName,
    const StateManager::FeatureState& fs) {
    const std::string slot = fs.active_slot.empty() ? fs.primary_slot : fs.active_slot;
    const std::string version =
        fs.active_version.empty() ? fs.primary_version : fs.active_version;
    std::string image = fs.primary_image;
    if (image.empty() && !fs.shadow_image.empty() && fs.shadow_enabled) {
        image = fs.shadow_image;
    }
    if (image.empty()) {
        image = defaultFleetImage(domainImageKey, version);
    }

    out << prefix << "_slot " << slot << "\n";
    out << prefix << "_version " << version << "\n";
    out << prefix << "_mode primary\n";
    out << prefix << "_enabled " << fleetEnabledToken(fs) << "\n";
    out << prefix << "_image " << image << "\n";
    out << prefix << "_binary_path active/" << binaryName << "\n";
    out << "\n";
}

bool isManagedFeatureToken(const std::string& featureName) {
    return kManagedFeatures.count(featureName) != 0;
}

void emitFleetDeployBlock(std::ostream& out, const StateManager::VehicleState& state) {
    out << "\n" << kFleetBlockStart << "\n";
    out << "fleet_schema 1\n\n";
    emitFleetDomainLines(out, "fleet_adas", "adas", "AdasFeature", state.adas);
    emitFleetDomainLines(out, "fleet_navigation", "navigation", "NavigationFeature", state.navigation);
    emitFleetDomainLines(out, "fleet_energy", "energy", "EvEnergyFeature", state.energy);
    out << kFleetBlockEnd << "\n";
}

bool rewriteCarConfigFromState(const std::string& configPath,
    const StateManager::VehicleState& state,
    const ConfigUpdater::Selection& selection,
    bool backupOriginal,
    std::string& error) {
    error.clear();
    std::ifstream in(configPath);
    if (!in.is_open()) {
        error = "failed to open " + configPath;
        return false;
    }

    std::ostringstream rebuilt;
    std::string line;
    bool insertedFeatures = false;
    bool skippingFleetBlock = false;

    while (std::getline(in, line)) {
        if (isFleetBlockStartLine(line)) {
            skippingFleetBlock = true;
            continue;
        }
        if (skippingFleetBlock) {
            if (isFleetBlockEndLine(line)) {
                skippingFleetBlock = false;
            }
            continue;
        }

        if (!line.empty() && line[0] != '#') {
            std::istringstream iss(line);
            std::string kw, name;
            if (iss >> kw >> name) {
                if (kw == "feature" && isManagedFeatureToken(name)) {
                    if (!insertedFeatures) {
                        emitFeatureBlock(rebuilt, selection);
                        insertedFeatures = true;
                    }
                    continue;
                }
            }
        }
        rebuilt << line << '\n';
    }

    if (!insertedFeatures) {
        rebuilt << "\n# Features (managed by DeploymentManager)\n";
        emitFeatureBlock(rebuilt, selection);
    }

    emitFleetDeployBlock(rebuilt, state);

    const std::string tmp = configPath + ".tmp";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) {
            error = "failed to open temp file " + tmp;
            return false;
        }
        out << rebuilt.str();
    }

    if (backupOriginal) {
        std::error_code bec;
        if (std::filesystem::exists(configPath, bec)) {
            const std::string bak = configPath + ".bak";
            std::filesystem::copy_file(configPath, bak, std::filesystem::copy_options::overwrite_existing, bec);
            if (bec) {
                std::filesystem::remove(tmp, ec);
                error = "failed to backup config to " + bak + ": " + bec.message();
                return false;
            }
        }
    }

    std::filesystem::rename(tmp, configPath, ec);
    if (ec) {
        error = "failed to replace config " + configPath + " from temp: " + ec.message();
        return false;
    }
    return true;
}

}  // namespace

bool ConfigUpdater::isManagedFeatureToken(const std::string& featureName) {
    return kManagedFeatures.count(featureName) != 0;
}

bool ConfigUpdater::loadSelection(const std::string& selectionPath, Selection& out) {
    std::ifstream in(selectionPath);
    if (!in.is_open()) {
        return false;
    }
    std::string key, val;
    while (in >> key >> val) {
        if (key == "ADAS") {
            out.adas = val;
        } else if (key == "NAV") {
            out.nav = val;
        } else if (key == "ENERGY") {
            out.energy = val;
        }
    }
    return true;
}

bool ConfigUpdater::saveSelection(const std::string& selectionPath, const Selection& s) {
    std::ofstream out(selectionPath);
    if (!out.is_open()) {
        return false;
    }
    out << "# Active car-line version per domain (A | B). Edited by DeploymentManager.\n";
    out << "ADAS " << s.adas << "\n";
    out << "NAV " << s.nav << "\n";
    out << "ENERGY " << s.energy << "\n";
    return true;
}

bool ConfigUpdater::rewriteManagedFeatureBlock(const std::string& configPath, const Selection& s,
    bool backupOriginal,
    std::string& error) {
    error.clear();
    std::ifstream in(configPath);
    if (!in.is_open()) {
        error = "failed to open " + configPath;
        return false;
    }

    std::ostringstream rebuilt;
    std::string line;
    bool insertedFeatures = false;
    bool skippingFleetBlock = false;

    while (std::getline(in, line)) {
        if (isFleetBlockStartLine(line)) {
            skippingFleetBlock = true;
            continue;
        }
        if (skippingFleetBlock) {
            if (isFleetBlockEndLine(line)) {
                skippingFleetBlock = false;
            }
            continue;
        }

        if (!line.empty() && line[0] != '#') {
            std::istringstream iss(line);
            std::string kw, name;
            if (iss >> kw >> name) {
                if (kw == "feature" && isManagedFeatureToken(name)) {
                    if (!insertedFeatures) {
                        emitFeatureBlock(rebuilt, s);
                        insertedFeatures = true;
                    }
                    continue;
                }
            }
        }
        rebuilt << line << '\n';
    }

    if (!insertedFeatures) {
        rebuilt << "\n# Features (managed by DeploymentManager)\n";
        emitFeatureBlock(rebuilt, s);
    }

    const std::string tmp = configPath + ".tmp";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) {
            error = "failed to open temp file " + tmp;
            return false;
        }
        out << rebuilt.str();
    }

    if (backupOriginal) {
        std::error_code bec;
        if (std::filesystem::exists(configPath, bec)) {
            const std::string bak = configPath + ".bak";
            std::filesystem::copy_file(configPath, bak, std::filesystem::copy_options::overwrite_existing, bec);
            if (bec) {
                std::filesystem::remove(tmp, ec);
                error = "failed to backup config to " + bak + ": " + bec.message();
                return false;
            }
        }
    }

    std::filesystem::rename(tmp, configPath, ec);
    if (ec) {
        error = "failed to replace config " + configPath + " from temp: " + ec.message();
        return false;
    }
    return true;
}

bool ConfigUpdater::applyToConfig(const std::string& configPath, const Selection& s) {
    std::string err;
    return rewriteManagedFeatureBlock(configPath, s, false, err);
}

bool ConfigUpdater::applyStateToCarConfig(const std::string& configPath, const StateManager::VehicleState& state,
    std::string& error) {
    error.clear();
    if (!validateActiveSlot(state.adas.active_slot, "ADAS", error)) {
        return false;
    }
    if (!validateActiveSlot(state.navigation.active_slot, "Navigation", error)) {
        return false;
    }
    if (!validateActiveSlot(state.energy.active_slot, "Energy", error)) {
        return false;
    }

    Selection s;
    s.adas = state.adas.active_slot;
    s.nav = state.navigation.active_slot;
    s.energy = state.energy.active_slot;
    return rewriteCarConfigFromState(configPath, state, s, true, error);
}

}  // namespace sdv::platform::deployment
