#pragma once

#include <optional>
#include <vector>
#include <string>

namespace sdv::platform::deployment {

class StateManager {
public:
    // Vehicle ACK (file-based, CLI-driven in this step).
    // This models a minimal "vehicle -> deployment manager" acknowledgement record without
    // implementing any MQTT/HTTP transport. The record is persisted in deploy/state/<car>.json.
    struct AckRecord {
        std::string status;     // DOWNLOAD_STARTED | DOWNLOAD_COMPLETED | CHECKSUM_OK | CHECKSUM_FAILED |
                                // INSTALL_STARTED | INSTALL_COMPLETED | INSTALL_FAILED | ROLLBACK_COMPLETED
        std::string at;         // UTC ISO-8601 timestamp string
        std::string message;    // free-form text (human readable)
        std::string feature;    // ADAS | Navigation | Energy
        std::string version;    // optional version token (e.g. v2.0 or 2.0; caller-controlled in this step)
        std::string deploy_id;  // optional deploy id / correlation id
    };

    // Rollback metadata tracked in `deploy/state/<car>.json` (per-feature).
    // This is purely metadata/policy in this step; it does not execute docker containers.
    struct RollbackStackEntry {
        std::string slot;           // legacy compatibility (A|B); may be empty for docker-only workflows
        std::string version;        // e.g. v2.0
        std::string docker_image;   // optional (empty when unknown)
        std::string previous_role;  // e.g. stable/candidate (optional)
        std::string recorded_at;    // UTC ISO-8601
        std::string reason;         // e.g. "promoted_to_primary"
        std::string status;         // e.g. "candidate" | "stable" (metadata only)
    };

    struct BlockedVersionEntry {
        std::string slot;          // legacy compatibility (A|B); may be empty
        std::string version;       // e.g. v2.0
        std::string docker_image;  // optional
        std::string blocked_at;    // UTC ISO-8601
        std::string reason;        // free-form
        std::string source;        // shadow_validation_failed | primary_runtime_failed
    };

    struct FeatureState {
        // Legacy naming (kept for JSON/UI/API compatibility):
        // - active_* is conceptually the same as primary_* in the Docker-image based terminology.
        // - active_slot remains a legacy A/B selector (version_map slot) and is NOT the same concept as Shadow.
        std::string active_slot;     // "A" | "B"
        std::string active_version;  // e.g. v0.1

        // Preferred naming (alias of legacy active_*):
        // - primary_* is the "currently controlling" version.
        // - When parsing JSON: if only active_* exists, primary_* is populated with the same value; and vice versa.
        std::string primary_slot;     // legacy slot (A|B); same as active_slot
        std::string primary_version;  // same as active_version
        std::string primary_image;    // optional docker image ref/tag (empty when unknown)
        std::string flag;            // "ON" | "OFF"

        bool shadow_enabled{false};
        std::string shadow_slot;     // "" | "A" | "B"
        std::string shadow_version;  // ""
        std::string shadow_image;    // optional docker image ref/tag (empty when unknown)

        // Shadow validation/progress (optional; missing in legacy state files).
        // Shadow is a candidate verification flow: it must NOT change Primary selection until Promote.
        std::string shadow_status;             // DISABLED | PENDING_IMAGE | RUNNING | VALIDATING | VALIDATION_PASSED | VALIDATION_FAILED
        std::string shadow_started_at;         // UTC ISO-8601 (when shadow became RUNNING/VALIDATING)
        std::string shadow_validation_result;  // PASSED | FAILED | UNKNOWN (optional)
        std::string shadow_failure_reason;     // optional human-readable reason when failed

        bool kill_switch{false};
        std::string kill_reason;

        std::string fallback_slot;    // "A" | "B"
        std::string fallback_version; // e.g. v0.1

        /// Staged fleet slot/version (written by deferred `fleet-deploy vehicle`); applied on next car start.
        std::string pending_active_slot;     // "" | "A" | "B"
        std::string pending_active_version;  // e.g. v0.2

        // Optional per-feature flags (only populated when present in JSON).
        std::string realtime_route_flag;       // Navigation only
        std::string charging_recommend_flag;   // Navigation + Energy
        std::string advanced_range_flag;       // Energy only

        // Optional rollback policy metadata (missing in legacy state files).
        std::vector<RollbackStackEntry> rollback_stack;   // most-recent first (top at index 0)
        std::vector<BlockedVersionEntry> blocked_versions;
        std::vector<BlockedVersionEntry> quarantine;      // optional; separate bucket from blocked_versions
    };

    struct OtaState {
        std::string download_status;
        std::string install_status;
        std::string vehicle_mode;
        std::string ignition;
        std::string update_mode;
        // Deployment-manager tracked OTA phase (optional; missing in legacy state files).
        // This is NOT the same as vehicle ACK (last_ack). See README: ACK is vehicle-reported,
        // ota_phase is deployment-manager progress tracking.
        std::string ota_phase;        // e.g. IDLE, DOWNLOAD_STARTED, VERIFYING, INSTALL_COMPLETED, ...
        std::string failure_reason;   // optional reason when ota_phase indicates failure
        std::string gear;
        int speed{0};
        std::string parking_brake;
        int battery{0};
        bool driver_approved{false};
        std::string network;
        bool target_system_running{false};
    };

    struct VehicleState {
        std::string car_id;
        std::string model;
        std::string region;
        std::string group;
        std::string status;

        // Online/heartbeat metadata (optional; missing in legacy state files).
        // registered vehicles = vehicles.json entries (fleet registry)
        // online vehicles     = last_seen_at within a threshold (see deploy docs)
        std::string last_seen_at;      // UTC ISO-8601 timestamp string
        std::string heartbeat_source;  // ack | wsl_pgrep | mqtt | http | unknown (string for future extension)
        std::string online_status;     // online | offline | unknown (optional; dashboard/server may compute instead)

        FeatureState adas;
        FeatureState navigation;
        FeatureState energy;
        OtaState ota;

        // Optional: last vehicle ACK record (missing in legacy state files).
        std::optional<AckRecord> last_ack;
    };

    explicit StateManager(std::string workingDirectory = ".");

    std::optional<VehicleState> loadState(const std::string& car_id, std::string& error) const;
    bool saveState(const std::string& car_id, const VehicleState& state, std::string& error) const;

    bool updateFeatureSlot(const std::string& car_id, const std::string& feature,
        const std::string& slot, const std::string& version, std::string& error) const;
    /// Records pending slot/version only; does not change active_slot/active_version.
    bool updateFeaturePending(const std::string& car_id, const std::string& feature,
        const std::string& slot, const std::string& version, std::string& error) const;
    /// If pending version is set, copies pending → active and clears pending for that feature.
    bool promotePendingToActive(const std::string& car_id, const std::string& feature, std::string& error) const;
    bool updateFeatureFlag(const std::string& car_id, const std::string& feature,
        const std::string& flag, std::string& error) const;
    bool updateKillSwitch(const std::string& car_id, const std::string& feature,
        bool enabled, const std::string& reason, std::string& error) const;
    bool updateShadowState(const std::string& car_id, const std::string& feature,
        bool enabled, const std::string& shadow_slot, const std::string& shadow_version,
        std::string& error) const;

    // OTA helpers (validate allowed enum-like values; modify ota only).
    bool updateOtaDownloadStatus(const std::string& car_id, const std::string& download_status,
        std::string& error) const;
    bool updateOtaInstallStatus(const std::string& car_id, const std::string& install_status,
        std::string& error) const;
    bool updateOtaUpdateMode(const std::string& car_id, const std::string& update_mode,
        std::string& error) const;
    bool updateDriverApproval(const std::string& car_id, bool approved, std::string& error) const;
    bool updateTargetSystemRunning(const std::string& car_id, bool running, std::string& error) const;

    // Bundled OTA update (atomic save): you can leave pointers null to keep existing values.
    bool updateOtaProgressForDeploy(const std::string& car_id,
        const std::string* download_status,
        const std::string* install_status,
        const std::string* update_mode,
        const bool* target_system_running,
        std::string& error) const;

    /// Updates deployment-manager tracked OTA phase (`ota.ota_phase`) and optional failure reason.
    /// This does not replace legacy ota fields; it complements them for clearer phase separation.
    bool updateOtaPhase(const std::string& car_id,
        const std::string& ota_phase,
        const std::string& failure_reason,
        std::string& error) const;

    /// Records a rollback candidate into feature.rollback_stack (most-recent-first) and caps length.
    bool pushRollbackStackEntry(const std::string& car_id,
        const std::string& feature,
        const RollbackStackEntry& entry,
        std::size_t maxDepth,
        std::string& error) const;

    /// Adds a blocked version entry (and best-effort removes it from rollback_stack).
    bool addBlockedVersion(const std::string& car_id,
        const std::string& feature,
        const BlockedVersionEntry& entry,
        std::string& error) const;

    /// Records a vehicle ACK into `deploy/state/<car_id>.json` under `last_ack`.
    /// This is a file-based stand-in for future MQTT/HTTP transport.
    bool recordAck(const std::string& car_id,
        const std::string& feature,
        const std::string& ack_status,
        const std::string& message,
        const std::string& version,
        const std::string& deploy_id,
        std::string& error) const;

    /// Quick sanity-check for the on-disk sample states; returns empty string when OK.
    std::string selfTest() const;

    /// Resolve fleet feature name to in-memory feature block (nullptr if unknown).
    static FeatureState* featureRef(VehicleState& s, const std::string& feature);
    static const FeatureState* featureRef(const VehicleState& s, const std::string& feature);

private:
    std::string workDir_;
    std::string stateDir_;

    std::string statePath(const std::string& car_id) const;

    static bool isValidCarId(const std::string& car_id);
    static bool isValidFeature(const std::string& feature);
    static bool isValidSlot(const std::string& slot, bool allowEmpty = false);
    static bool isValidFlag(const std::string& flag);
    static bool isValidDownloadStatus(const std::string& v);
    static bool isValidInstallStatus(const std::string& v);
    static bool isValidUpdateMode(const std::string& v);
    static bool isValidOtaPhase(const std::string& v);
    static bool isValidAckStatus(const std::string& v);
    static bool isValidBlockedSource(const std::string& v);
};

}  // namespace sdv::platform::deployment
