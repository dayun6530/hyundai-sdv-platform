#pragma once

#include "DeploymentManager/ConfigUpdater.h"
#include "DeploymentManager/DeploymentHistory.h"
#include "DeploymentManager/PackageInfo.h"
#include "DeploymentManager/StateManager.h"
#include "DeploymentManager/VehicleRegistry.h"
#include "DeploymentManager/VersionManager.h"

#include <map>
#include <string>
#include <vector>

namespace sdv::platform::deployment {

/// File-based Deployment Manager (prototype).
///
/// ## Responsibility (what this module DOES)
/// - **Package intake / register**: accept package artifacts under `deploy/packages/` and record what is available.
/// - **Validation**: lightweight checks on package file names and contents (demo-level; no PKI/OTA transport in this step).
/// - **Placement**: prepare the on-vehicle artifact layout (packages → staging → active), backup previous active.
/// - **State update**: write fleet/vehicle deployment state under `deploy/state/*.json` (and selection/config files).
/// - **Rollback metadata**: write/maintain rollback metadata under `deploy/rollback/`.
/// - **History**: append audit/history records under `deploy/history/` and `deploy/manifest.txt`.
///
/// ## Non-responsibility (what this module does NOT do)
/// - **Does not spawn/exec Feature processes** and does not own runtime lifecycle.
///   The **platform runtime / LifeCycleManager** reads `deploy/active/*` + state/config and decides what to execute.
///
/// Notes:
/// - "Release" helpers (select/rollback/sync-config/health-check) operate on files + state only; they do not
///   start/stop feature processes. Any "activation" effect requires the runtime to (re)load config / restart.
/// - Staging artifacts are intentionally retained (pre-validation / approval expansion).
class DeploymentManager {
public:
    /// Optional Docker runtime integration via \c scripts/ota_vehicle.sh (fleet-deploy trailing flag).
    struct FleetDockerRuntimeOptions {
        bool requested{false};
        /// \c candidate (default) runs deploy-shadow; \c promote runs ota_vehicle.sh promote.
        std::string mode{"candidate"};
        /// \c missing | \c always | empty (no pull; local image inspect only).
        std::string pull_policy;
    };

    struct DeployResult {
        std::string car_id;
        std::string feature;
        std::string target_slot;
        std::string target_version;
        bool success{false};
        std::string reason;  // empty if success
        bool docker_runtime_requested{false};
        std::string docker_runtime_mode;
        std::string docker_runtime_action;
        std::string docker_runtime_result;  // success | failed | skipped | empty
        std::string docker_runtime_reason;
        std::string docker_runtime_container;
        std::string docker_runtime_image;
        std::string docker_runtime_pull_policy;
        bool docker_pull_requested{false};
        /// Before fleet-deploy to candidate slot (legacy B): primary selection preserved for Docker reconcile.
        std::string staged_primary_slot;
        std::string staged_primary_version;
        /// True when fleet state JSON was updated by a successful deploy for this vehicle.
        bool state_updated{false};
        /// True when the caller requested \c --sync-config (per-target row still shows request intent).
        bool config_sync_requested{false};
        /// True only after a successful deploy and a successful \c syncCarConfigFromState for this \c car_id.
        bool config_synced{false};
        /// Non-empty when deploy succeeded, sync was requested, but config sync failed (state is not rolled back).
        std::string config_sync_error;
        /// Fleet rollback metadata (\c deploy/rollback/car_N/Feature.json) written before state mutation when deploy succeeds.
        bool rollback_metadata_saved{false};
        std::string rollback_metadata_path;  // repo-relative, e.g. deploy/rollback/car_1/ADAS.json
        std::string rollback_metadata_error; // phase-1 write failure (deploy aborted) or finalize write warning
        /// Internal: snapshot + paths for rollback JSON finalization after optional config sync.
        StateManager::FeatureState rollback_prev_feature{};
        bool rollback_prev_valid{false};
        std::string rollback_meta_id;
        std::string rollback_meta_timestamp;
        std::string rollback_meta_abs_path;
    };

    struct DeploySummary {
        std::string deploy_id;
        std::string timestamp;
        std::string target_type;  // vehicle | group | region | group_region
        std::string group;
        std::string region;
        std::string feature;
        std::string target_slot;
        std::string target_version;
        int total{0};
        int success{0};
        int failed{0};
        std::string failed_reason;  // e.g. "no target vehicles" (when total=0)
        std::map<std::string, int> reason_counts;  // stable order for JSON/logs
        std::vector<DeployResult> results;
        /// Set when \c fleet-deploy is invoked with \c --sync-config.
        bool sync_config_requested{false};
        bool docker_runtime_requested{false};
        std::string docker_runtime_mode;
        bool docker_pull_requested{false};
        std::string docker_pull_policy;
    };

    /// \p workingDirectory Repository root (contains deploy/, config/). Usually current working directory.
    explicit DeploymentManager(std::string workingDirectory);

    // -------------------------------------------------------------------------
    // Package intake / validation / placement (artifacts only; no process exec)
    // -------------------------------------------------------------------------

    /// Deploy pipeline: packages → staging → active. Previous active is backed up.
    bool deployExecutable(const std::string& execBasename);

    // -------------------------------------------------------------------------
    // Release helpers (selection/config sync/rollback) — file/state operations only
    // The runtime (LifeCycleManager) is responsible for actually executing binaries.
    // -------------------------------------------------------------------------

    /// Set which A|B line should be considered active for ADAS | NAV | ENERGY and update selection + config files.
    bool selectVersion(const std::string& domain, const std::string& ab,
        const std::string& platformConfigPath);

    /// Pop domain stack and apply previous active version to config.
    bool rollbackDomain(const std::string& domain, const std::string& platformConfigPath);

    /// Re-write config from deploy/active_selection.conf only.
    bool syncConfig(const std::string& platformConfigPath);

    /// Rewrite \c config/car/<car_id>.conf managed \c feature lines from \c deploy/state/<car_id>.json.
    /// Allowed \p car_id: \c car_1 … \c car_10 only. Does not modify fleet state JSON.
    /// When \p quietLog is true, no stdout/stderr logging (caller uses \p outMessage).
    bool syncCarConfigFromState(const std::string& car_id, std::string& outMessage, bool quietLog = false);

    void printStatus(const std::string& platformConfigPath) const;
    void printInventory() const;

    const std::string& workDir() const { return workDir_; }

    // -------------------------------------------------------------------------
    // Fleet state operations (deploy/state JSON + optional config sync)
    // These APIs write deployment state and metadata; they still do not execute features.
    // -------------------------------------------------------------------------

    // Fleet-based deploy helpers (default: vehicle state only). Optional \c --sync-config on \c fleet-deploy
    // may sync \c config/car/*.conf for successful targets via \c syncCarConfigFromState.
    bool deployToVehicle(const std::string& car_id, const std::string& feature,
        const std::string& target_slot, const std::string& target_version, std::string& outMessage);
    bool deployToGroup(const std::string& group, const std::string& feature,
        const std::string& target_slot, const std::string& target_version);
    bool deployToRegion(const std::string& region, const std::string& feature,
        const std::string& target_slot, const std::string& target_version);
    bool deployToGroupAndRegion(const std::string& group, const std::string& region, const std::string& feature,
        const std::string& target_slot, const std::string& target_version);

    DeploySummary deployToGroupWithSummary(const std::string& group, const std::string& feature,
        const std::string& target_slot, const std::string& target_version, bool sync_config_after_success = false,
        const FleetDockerRuntimeOptions* docker_runtime = nullptr);
    DeploySummary deployToRegionWithSummary(const std::string& region, const std::string& feature,
        const std::string& target_slot, const std::string& target_version, bool sync_config_after_success = false,
        const FleetDockerRuntimeOptions* docker_runtime = nullptr);
    DeploySummary deployToGroupAndRegionWithSummary(const std::string& group, const std::string& region,
        const std::string& feature, const std::string& target_slot, const std::string& target_version,
        bool sync_config_after_success = false, const FleetDockerRuntimeOptions* docker_runtime = nullptr);
    DeploySummary deployToVehicleWithSummary(const std::string& car_id, const std::string& feature,
        const std::string& target_slot, const std::string& target_version, bool sync_config_after_success = false,
        bool defer_native_until_restart = false, const FleetDockerRuntimeOptions* docker_runtime = nullptr);

    /// For \p car_id: for each feature with non-empty pending version, \c deployExecutable from \c version_map,
    /// then promote pending → active in \c deploy/state. No-op when nothing pending. CWD must be platform root.
    /// When \p only_feature is non-empty (ADAS|Navigation|Energy), only that feature is applied.
    bool applyPendingNativeForCar(const std::string& car_id, std::string& outMessage,
        const std::string& only_feature = "");

    /// Metadata-only promote: treat current Shadow as validated and promote it to Primary in deploy/state.
    /// This does NOT execute/rename docker containers; it only synchronizes state fields:
    /// - primary/active version (+ optional image), rollback_stack, and shadow disabled.
    bool fleetPromoteShadowVehicle(const std::string& car_id, const std::string& feature, std::string& outMessage);

    // Fleet deploy reporting helpers (kept separate so UI/API can reuse the exact format).
    void printFleetDeploySummary(const DeploySummary& summary) const;
    std::string fleetDeploySummaryToJson(const DeploySummary& summary) const;

    // Development helper: validates target selection and demonstrates OTA safety gating.
    bool selfTestFleetDeploy();

    /// Result of \c fleet-rollback vehicle (for text + JSON summary).
    struct FleetRollbackSummary {
        std::string action{"fleet-rollback"};
        std::string scope{"vehicle"};
        std::string car_id;
        std::string feature;
        std::string restored_slot;
        std::string restored_version;
        bool sync_config_requested{false};
        bool config_synced{false};
        std::string config_sync_error;
        std::string result;  // success | failed | partial_success
        std::string reason;
        std::string metadata_path;
        std::string state_file;
        std::string config_file;
    };

    /// Restore one feature in \c deploy/state from \c deploy/rollback metadata (vehicle scope only).
    /// Fills \p summary for every outcome. Returns \c true only when state is restored and optional \c --sync-config
    /// sync succeeds; returns \c false on any failure or \c partial_success (state restored, config sync failed).
    bool fleetRollbackVehicle(const std::string& car_id,
        const std::string& feature,
        bool sync_config,
        FleetRollbackSummary& summary,
        std::string& errorMessage,
        const std::string& target_version = std::string{});

    void printFleetRollbackSummary(const FleetRollbackSummary& summary) const;
    std::string fleetRollbackSummaryToJson(const FleetRollbackSummary& summary) const;

    /// Result of \c fleet-flag vehicle (administrative Feature Flag ON/OFF in deploy/state only).
    struct FleetFlagSummary {
        std::string action{"fleet-flag"};
        std::string scope{"vehicle"};
        std::string car_id;
        std::string feature;
        std::string previous_flag;
        std::string new_flag;
        std::string active_slot;
        std::string active_version;
        bool sync_config_requested{false};
        bool config_synced{false};
        std::string config_sync_error;
        std::string result;  // success | failed | partial_success
        std::string reason;
        std::string state_file;
        std::string config_file;
        /// Explains that flag lives in state JSON; optional config sync aligns active_slot block only.
        std::string note;
    };

    /// Sets only the feature \c flag in \c deploy/state via \c StateManager::updateFeatureFlag (no slot/version/kill/shadow changes).
    /// Returns \c true only on full success; \c false on failure or \c partial_success (state updated, config sync failed).
    bool fleetFlagVehicle(const std::string& car_id, const std::string& feature, const std::string& new_flag,
        bool sync_config, FleetFlagSummary& summary, std::string& errorMessage);

    void printFleetFlagSummary(const FleetFlagSummary& summary) const;
    std::string fleetFlagSummaryToJson(const FleetFlagSummary& summary) const;

    /// Result of \c fleet-kill vehicle (emergency kill_switch + policy-driven fallback in deploy/state).
    struct FleetKillSummary {
        std::string action{"fleet-kill"};
        std::string scope{"vehicle"};
        std::string car_id;
        std::string feature;
        std::string kill_reason;
        std::string previous_slot;
        std::string previous_version;
        std::string new_slot;
        std::string new_version;
        std::string previous_flag;
        std::string new_flag;
        bool kill_switch{false};
        /// Post-operation values (populated for UI; empty when not used by policy).
        std::string realtime_route_flag;
        std::string charging_recommend_flag;
        std::string advanced_range_flag;
        bool sync_config_requested{false};
        bool config_synced{false};
        std::string config_sync_error;
        std::string result;  // success | failed | partial_success
        std::string reason;
        std::string state_file;
        std::string config_file;
        std::string policy_note;
        /// fleet-kill vs fleet-flag; notes that EventBus integration is future work.
        std::string note;
    };

    /// Emergency kill: sets \c kill_switch and \c kill_reason, applies per-feature fallback policy, does not touch shadow fields.
    /// Returns \c true only on full success; \c false on failure or \c partial_success (state saved, config sync failed).
    bool fleetKillVehicle(const std::string& car_id, const std::string& feature, const std::string& kill_reason,
        bool sync_config, FleetKillSummary& summary, std::string& errorMessage);

    void printFleetKillSummary(const FleetKillSummary& summary) const;
    std::string fleetKillSummaryToJson(const FleetKillSummary& summary) const;

    /// Result of \c fleet-shadow vehicle (shadow_enabled / shadow_slot / shadow_version in deploy/state only).
    struct FleetShadowSummary {
        std::string action{"fleet-shadow"};
        std::string scope{"vehicle"};
        std::string car_id;
        std::string feature;
        std::string shadow_action;  // ON | OFF
        bool previous_shadow_enabled{false};
        std::string previous_shadow_slot;
        std::string previous_shadow_version;
        bool new_shadow_enabled{false};
        std::string new_shadow_slot;
        std::string new_shadow_version;
        std::string active_slot;
        std::string active_version;
        std::string flag;
        bool kill_switch{false};
        std::string result;  // success | failed
        std::string reason;
        std::string state_file;
        std::string policy_note;
        std::string note;
    };

    /// Shadow mode: updates shadow fields via \c StateManager::updateShadowState; ON uses slot B version from \c VersionManager (version_map).
    /// Does not change active_slot, active_version, flag, or kill_switch; does not modify config files.
    bool fleetShadowVehicle(const std::string& car_id, const std::string& feature, const std::string& shadow_action,
        FleetShadowSummary& summary, std::string& errorMessage);

    void printFleetShadowSummary(const FleetShadowSummary& summary) const;
    std::string fleetShadowSummaryToJson(const FleetShadowSummary& summary) const;

    /// Prints \c [SUMMARY_JSON] with optional \c history_saved / \c history_error, and appends one line to \c deploy/history/fleet_history.jsonl.
    void emitSummaryJsonWithFleetHistory(const std::string& summaryJson) const;

    /// Minimal JSON for \c sync-config-car fleet history (separate from fleet-* summary structs).
    std::string buildSyncConfigCarSummaryJson(const std::string& car_id, bool success, const std::string& message) const;

private:
    void applyPostDeployConfigSync(DeployResult& r, bool sync_requested);
    void resyncCarConfigAfterDockerRuntime(DeployResult& r);
    void applyPostDeployDockerRuntime(DeployResult& r, bool defer_native_deploy,
        const FleetDockerRuntimeOptions& options);

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

    static bool isValidFleetFeature(const std::string& feature);
    static bool isValidFleetSlot(const std::string& slot);
    static bool checkOtaSafety(const StateManager::VehicleState& s, std::string& reason);
    bool validateTargetVersion(const VersionManager& vm, const std::string& feature,
        const std::string& slot, const std::string& version, std::string& reason) const;

    DeployResult deployToVehicleInternal(const std::string& car_id, const std::string& feature,
        const std::string& target_slot, const std::string& target_version, bool sync_config_requested,
        bool defer_native_until_restart = false, bool candidate_docker_shadow_only = false);

    void ensurePrimaryContainerForCandidateDeploy(DeployResult& r, const std::string& dockerImage);

    void finalizeFleetRollbackMetadata(DeployResult& r);
    void printDeploySummary(const DeploySummary& summary) const;

    static std::string jsonEscape(const std::string& value);
    static std::string generateDeployId();
    static std::string currentTimestamp();
    std::string deploySummaryToJson(const DeploySummary& summary) const;
    void printDeploySummaryJson(const DeploySummary& summary) const;

    /// Normalizes summary JSON for JSONL (e.g. \c fleet-deploy action key); ensures top-level \c timestamp exists.
    std::string prepareFleetHistoryLine(std::string summaryJson) const;
    bool appendFleetHistoryLineRaw(const std::string& line, std::string& error) const;
    static std::string appendHistoryMetaToSummaryJson(std::string json, bool history_saved, const std::string& history_error);
};

}  // namespace sdv::platform::deployment
