#pragma once

#include "DeploymentManager/StateManager.h"

#include <string>

namespace sdv::platform::deployment {

/// Rewrites the `feature` section of a LifeCycleMgr config while preserving
/// address / ab_mode / communication lines and overall formatting style.
class ConfigUpdater {
public:
    /// Domain codes: ADAS, NAV, ENERGY — values must be "A" or "B".
    struct Selection {
        std::string adas{"A"};
        std::string nav{"A"};
        std::string energy{"A"};
    };

    /// Loads selection from \p selectionPath (simple KEY VALUE lines).
    static bool loadSelection(const std::string& selectionPath, Selection& out);

    /// Persists \p selection to \p selectionPath.
    static bool saveSelection(const std::string& selectionPath, const Selection& s);

    /// Applies \p selection to \p configPath (reads whole file, writes atomically via temp file).
    static bool applyToConfig(const std::string& configPath, const Selection& s);

    /// Rewrites managed `feature` lines and the fleet deploy mirror block from \p state.
    /// On success, replaces \p configPath via temp file and writes a \p configPath + ".bak" copy of the previous file.
    /// On failure, \p error is set (open/validation/backup/rename).
    static bool applyStateToCarConfig(const std::string& configPath, const StateManager::VehicleState& state,
        std::string& error);

private:
    static bool isManagedFeatureToken(const std::string& featureName);
    static bool rewriteManagedFeatureBlock(const std::string& configPath, const Selection& s, bool backupOriginal,
        std::string& error);
};

}  // namespace sdv::platform::deployment
