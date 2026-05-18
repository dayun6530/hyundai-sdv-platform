#pragma once

#include "DeploymentManager/CampaignDocument.h"

#include <string>
#include <vector>

namespace sdv::platform::deployment {

struct CampaignValidationResult {
    bool ok{false};
    std::string first_failure;
    std::vector<std::string> failures;
    std::vector<std::string> warnings;
    std::vector<std::string> resolved_car_ids;
    CampaignDocument document;
};

struct CampaignValidateOptions {
    /// When true, safety_requirements runtime checks (OTA READY, etc.) are warnings only.
    bool runtime_preconditions_as_warnings{false};
    /// When false, docker_runtime.enabled is not required (validate-only campaigns).
    bool require_docker_runtime{true};
    /// \c candidate or \c promote for campaign-run preflight; empty = full validate (campaign-validate).
    std::string run_phase;
};

/// Structural + policy validation (no deploy). Used by campaign-validate and campaign-run preflight.
CampaignValidationResult validateCampaign(const std::string& workDir, const CampaignDocument& doc,
    const CampaignValidateOptions& options = {});

/// Load + validate; \p campaignJsonPath relative to \p workDir.
CampaignValidationResult validateCampaignFile(const std::string& workDir, const std::string& campaignJsonPath,
    const CampaignValidateOptions& options = {});

}  // namespace sdv::platform::deployment
