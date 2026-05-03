#include "DeploymentManager/ConfigUpdater.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace sdv::platform::deployment {

namespace {

const std::unordered_set<std::string> kManagedFeatures = {
    // Version-neutral car processes (demo default)
    "NavigationFeature",
    "NavigationFeatureShadow",
    "AdasFeature",
    "AdasFeatureShadow",
    "EvEnergyFeature",
    "EvEnergyFeatureShadow",
    // Legacy A/B line names (still rewritten when present)
    "NavigationFeature_A",
    "NavigationFeature_A_Shadow",
    "NavigationFeature_B",
    "NavigationFeature_B_Shadow",
    "AdasFeature_A",
    "AdasFeature_A_Shadow",
    "AdasFeature_B",
    "AdasFeature_B_Shadow",
    "EvEnergyFeature_A",
    "EvEnergyFeature_A_Shadow",
    "EvEnergyFeature_B",
    "EvEnergyFeature_B_Shadow",
    "WatchDog",
    "FeatureEvaluator",
};

void emitFeatureBlock(std::ostream& out, const ConfigUpdater::Selection& s) {
    (void)s;
    // Always emit logical feature binaries (logs: AdasFeature.log / AdasFeatureShadow.log).
    // deploy/state active_slot (A|B) remains for fleet / version_map rollouts only.
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

    while (std::getline(in, line)) {
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
    return rewriteManagedFeatureBlock(configPath, s, true, error);
}

}  // namespace sdv::platform::deployment
