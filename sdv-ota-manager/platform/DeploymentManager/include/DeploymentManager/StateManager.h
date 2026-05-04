#pragma once

#include <optional>
#include <string>

namespace sdv::platform::deployment {

class StateManager {
public:
    struct FeatureState {
        std::string active_slot;     // "A" | "B"
        std::string active_version;  // e.g. v0.1
        std::string flag;            // "ON" | "OFF"

        bool shadow_enabled{false};
        std::string shadow_slot;     // "" | "A" | "B"
        std::string shadow_version;  // ""

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
    };

    struct OtaState {
        std::string download_status;
        std::string install_status;
        std::string vehicle_mode;
        std::string ignition;
        std::string update_mode;
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

        FeatureState adas;
        FeatureState navigation;
        FeatureState energy;
        OtaState ota;
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
};

}  // namespace sdv::platform::deployment
