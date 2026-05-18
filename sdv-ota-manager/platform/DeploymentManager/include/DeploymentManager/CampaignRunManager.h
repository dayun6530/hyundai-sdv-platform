#pragma once

#include "DeploymentManager/DeploymentManager.h"

#include <string>

namespace sdv::platform::deployment {

/// Executes campaign phases by reusing internal fleet-deploy paths (no subprocess).
class CampaignRunManager {
public:
    explicit CampaignRunManager(std::string workDir = ".");

    /// \p phase must be \c candidate (promote is not supported in 9-5).
    int runCampaignFile(const std::string& campaignJsonPath, const std::string& phase, DeploymentManager& deployment);

private:
    std::string workDir_;
};

}  // namespace sdv::platform::deployment
