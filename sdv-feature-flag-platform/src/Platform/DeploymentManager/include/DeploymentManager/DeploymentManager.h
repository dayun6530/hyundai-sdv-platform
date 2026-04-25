#pragma once

#include "DeploymentManager/ConfigUpdater.h"
#include "DeploymentManager/DeploymentHistory.h"
#include "DeploymentManager/PackageInfo.h"

#include <string>

namespace sdv::platform::deployment {

/// File-based Deployment Manager (prototype).
/// - Deployment: package intake / validation / placement (packages → staging → active)
/// - Release: version selection / config sync / activation health check / rollback
///   - Note: staging artifacts are intentionally retained (pre-validation / approval expansion).
class DeploymentManager {
public:
    /// \p workingDirectory Repository root (contains deploy/, config/). Usually current working directory.
    explicit DeploymentManager(std::string workingDirectory);

    /// Deploy pipeline: packages → staging → active. Previous active is backed up.
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
    struct ValidationInfo {
        std::string result;  // "success" | "failed"
        std::string reason;  // short human-readable reason
        std::string hash;    // lightweight checksum/hash (optional)
    };

    struct HealthCheckInfo {
        std::string result;  // "success" | "failed"
        std::string reason;  // short human-readable reason
    };

    struct InventoryRecord {
        std::string stableVersion{"A"};
        std::string candidateVersion{"A"};  // Deployment result: last successfully staged candidate.
        std::string activeVersion{"A"};     // Release result: currently selected version.
        std::string lastDeploymentStatus{"STABLE"};  // Current summary across deployment/release flow.
        std::string lastDeployedAt{};       // Last successful deploy/select/rollback transition time.
        bool rollbackAvailable{false};      // Stack-based hint only; not a full backup/health guarantee.
    };

    DeploymentHistory history_;
    std::string workDir_;
    std::string packagesDir_;
    std::string stagingDir_;
    std::string activeDir_;
    std::string backupDir_;
    std::string metadataDir_;
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
    std::string inventoryFile(const std::string& domain) const;
    bool loadInventoryRecord(const std::string& domain, InventoryRecord& out) const;
    bool saveInventoryRecord(const std::string& domain, const InventoryRecord& record) const;
    bool updateInventoryOnDeploy(const PackageInfo& info, const std::string& deployStatus);
    bool updateInventoryOnSelect(const std::string& domain, const std::string& ab);
    bool updateInventoryOnRollback(const std::string& domain, const std::string& ab);
    bool updateInventoryStatusOnly(const std::string& domain, const std::string& status);
    bool hasRollbackHistory(const std::string& domain) const;
    static std::string expectedPrimaryExec(const std::string& domain, const std::string& ab);
    static bool configContainsExpectedPrimary(const std::string& configPath,
        const std::string& domain,
        const std::string& ab);
    bool runActivationHealthCheck(const std::string& domain,
        const std::string& ab,
        const std::string& platformConfigPath,
        HealthCheckInfo& out) const;

    static std::string nowTimestampForFilename();
    static bool copyFileWithPerms(const std::string& src, const std::string& dst);
    bool backupExistingActiveIfAny(const std::string& execBasename);
    static bool validatePackageFile(const std::string& path, ValidationInfo& out);
    bool writeDeployMetadataPlaceholder(const PackageInfo& info,
        const std::string& stagingPath,
        const std::string& activePath,
        const ValidationInfo& validation) const;
};

}  // namespace sdv::platform::deployment
