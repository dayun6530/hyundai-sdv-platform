#pragma once

#include <string>

namespace sdv::platform::deployment {

/// Parsed campaign JSON (shared by validate and run).
struct CampaignDocument {
    std::string campaign_id;
    std::string feature;
    std::string target_version;
    std::string target_role;
    std::string legacy_slot;
    std::string package_id;
    std::string package_path;
    std::string manifest_path;
    std::string docker_image;
    std::string artifact_type;
    std::string rollout_policy_id;
    std::string target_selector_json;
    std::string safety_json;
    std::string execution_json;
    std::string docker_runtime_json;
    bool approval_required{false};
    bool docker_runtime_enabled{false};
    std::string candidate_pull_policy;
    std::string promote_mode;
    std::string promote_policy;
    bool promote_separate_run{false};
    bool sync_config{false};
    bool no_defer_native{false};
};

struct CampaignLoadResult {
    bool ok{false};
    std::string error;
    CampaignDocument document;
};

/// Loads and parses a campaign file relative to \p workDir (platform root).
CampaignLoadResult loadCampaignFromFile(const std::string& workDir, const std::string& campaignJsonPath);

/// Legacy slot for fleet-deploy: candidate → B, primary → A; explicit legacy_slot wins when set.
std::string fleetDeploySlotForCampaign(const CampaignDocument& doc);

/// Accepts primary/candidate or internal legacy A/B (case-insensitive) for CLI/API compatibility.
std::string normalizeFleetDeploySlotArg(const std::string& slotOrRole);

}  // namespace sdv::platform::deployment
