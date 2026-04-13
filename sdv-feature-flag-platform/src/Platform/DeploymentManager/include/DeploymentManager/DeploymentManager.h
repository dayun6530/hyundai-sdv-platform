#pragma once

#include "DeploymentManager/ConfigUpdater.h"
#include "DeploymentManager/DeploymentHistory.h"
#include "DeploymentManager/PackageInfo.h"

#include <string>

namespace sdv::platform::deployment {

/// File-based deploy pipeline: intake → active copy, version selection, config sync, rollback stacks.
class DeploymentManager {
public:
    /// \p workingDirectory Repository root (contains deploy/, config/). Usually current working directory.
    explicit DeploymentManager(std::string workingDirectory);

    /// Copy \p execBasename from deploy/packages/ to deploy/active/ and record history + manifest.
    bool deployExecutable(const std::string& execBasename);

    /// Set which A|B line runs for ADAS | NAV | ENERGY, update selection file and platform config.
    bool selectVersion(const std::string& domain, const std::string& ab,
        const std::string& platformConfigPath);

    /// Pop domain stack and apply previous active version to config.
    bool rollbackDomain(const std::string& domain, const std::string& platformConfigPath);

    /// Re-write config from deploy/active_selection.conf only.
    bool syncConfig(const std::string& platformConfigPath);

    void printStatus(const std::string& platformConfigPath) const;
    void printInventory() const;

    const std::string& workDir() const { return workDir_; }

private:
    DeploymentHistory history_;
    std::string workDir_;
    std::string packagesDir_;
    std::string activeDir_;
    std::string selectionPath_;
    std::string stacksDir_;
    std::string manifestPath_;

    bool ensureDirs();
    static bool safeBasename(const std::string& name);
    static bool inferPackage(const std::string& execBasename, PackageInfo& out);
    bool loadOrInitSelection(ConfigUpdater::Selection& s);
    bool pushStack(const std::string& domain, const std::string& ab);
    bool popStack(const std::string& domain, std::string& previousAb);
    std::string stackFile(const std::string& domain) const;
    void appendManifest(const PackageInfo& info);
};

}  // namespace sdv::platform::deployment
