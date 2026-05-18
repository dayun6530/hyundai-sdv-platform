#pragma once

#include <string>

namespace sdv::platform::deployment {

/// Read-only campaign + rollout policy validation (no deploy, no state writes).
class CampaignValidator {
public:
    struct Summary {
        int ok{0};
        int warn{0};
        int fail{0};
        bool passed() const { return fail == 0; }
    };

    explicit CampaignValidator(std::string workDir = ".");

    /// Validates \p campaignJsonPath (relative to \p workDir_ or absolute).
    /// Prints [CAMPAIGN_VALIDATE], [OK], [WARN], [FAIL], [SUMMARY] lines to stdout/stderr.
    /// Returns 0 when fail==0, else 1.
    int validateCampaignFile(const std::string& campaignJsonPath) const;

private:
    std::string workDir_;
};

}  // namespace sdv::platform::deployment
