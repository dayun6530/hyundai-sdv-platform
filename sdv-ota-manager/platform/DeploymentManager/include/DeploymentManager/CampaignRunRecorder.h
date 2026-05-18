#pragma once

#include "DeploymentManager/CampaignDocument.h"
#include "DeploymentManager/DeploymentManager.h"

#include <string>
#include <vector>

namespace sdv::platform::deployment {

struct CampaignRunVehicleRecord {
    std::string car_id;
    std::string result;
    std::string reason;
    std::string deploy_id;
    bool state_updated{false};
    bool config_synced{false};
    std::string docker_runtime_result;
    std::string docker_runtime_mode;
    std::string docker_runtime_action;
    std::string docker_runtime_image;
    std::string docker_runtime_container;
    std::string state_file;
    std::string config_file;
    std::string history_path;
    std::string fleet_deploy_command;
};

struct CampaignRunRecord {
    int schema_version{1};
    std::string run_id;
    std::string campaign_id;
    std::string campaign_file;
    std::string rollout_policy_id;
    std::string phase;
    std::string status;
    std::string started_at;
    std::string finished_at;
    std::string feature;
    std::string target_version;
    std::string target_role;
    std::string legacy_slot;
    bool docker_requested{false};
    std::string docker_mode;
    bool docker_pull_requested{false};
    std::string docker_pull_policy;
    std::string docker_image;
    std::vector<std::string> resolved_targets;
    int aggregate_total{0};
    int aggregate_success{0};
    int aggregate_failed{0};
    std::vector<CampaignRunVehicleRecord> per_vehicle;
    std::vector<std::string> warnings;
    std::vector<std::string> failures;
};

struct CampaignRunSaveResult {
    bool ok{false};
    std::string error;
    std::string relative_path;
    std::string run_id;
};

/// Writes campaign run JSON under \p workDir/deploy/campaign_runs/.
class CampaignRunRecorder {
public:
    static std::string currentIsoTimestamp();
    static std::string compactTimestampForId();
    static std::string sanitizePathToken(const std::string& token);
    static std::string buildRunId(const std::string& compactTs, const std::string& campaign_id, const std::string& phase);
    static std::string buildFleetDeployCommand(const CampaignDocument& doc, const std::string& car_id,
        const std::string& slot, const std::string& phase);

    static CampaignRunVehicleRecord vehicleRecordFromDeploy(const std::string& car_id, const CampaignDocument& doc,
        const std::string& slot, const std::string& phase, const DeploymentManager::DeploySummary& summary);

    static CampaignRunSaveResult saveRunRecord(const std::string& workDir, const CampaignRunRecord& record);
};

}  // namespace sdv::platform::deployment
