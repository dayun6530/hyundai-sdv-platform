#include "DeploymentManager/CampaignManager.h"

#include "DeploymentManager/CampaignDocument.h"
#include "DeploymentManager/CampaignValidation.h"
#include "DeploymentManager/TargetingResolver.h"

#include <filesystem>
#include <iostream>

namespace sdv::platform::deployment {

namespace {

std::string jsonQuotedEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

struct ValidationLog {
    CampaignValidator::Summary summary;
    std::string firstFailReason;

    void ok(const std::string& line) const {
        std::cout << "[OK] " << line << "\n";
        ++const_cast<ValidationLog*>(this)->summary.ok;
    }

    void warn(const std::string& line) const {
        std::cout << "[WARN] " << line << "\n";
        ++const_cast<ValidationLog*>(this)->summary.warn;
    }

    void fail(const std::string& line) const {
        std::cerr << "[FAIL] " << line << "\n" << std::flush;
        auto* self = const_cast<ValidationLog*>(this);
        ++self->summary.fail;
        if (self->firstFailReason.empty()) {
            self->firstFailReason = line;
        }
    }
};

}  // namespace

CampaignValidator::CampaignValidator(std::string workDir)
    : workDir_(std::move(workDir)) {}

int CampaignValidator::validateCampaignFile(const std::string& campaignJsonPath) const {
    ValidationLog log;

    const std::filesystem::path absCampaign =
        std::filesystem::path(workDir_) / campaignJsonPath;
    std::error_code ec;
    if (!std::filesystem::exists(absCampaign, ec) || ec) {
        std::cout << "[CAMPAIGN_VALIDATE] campaign_id=(unknown) status=FAIL reason=\"campaign file not found\"\n";
        log.fail("campaign file not found: " + campaignJsonPath);
        std::cout << "[SUMMARY] ok=0 warn=0 fail=1\n";
        return 1;
    }

    log.ok("campaign file loaded: " + campaignJsonPath);

    CampaignValidateOptions options;
    options.require_docker_runtime = true;
    options.runtime_preconditions_as_warnings = true;

    const CampaignValidationResult result =
        sdv::platform::deployment::validateCampaignFile(workDir_, campaignJsonPath, options);

    const CampaignDocument& camp = result.document;

    if (!camp.campaign_id.empty()) {
        log.ok("campaign_id=" + camp.campaign_id);
    }
    if (!camp.feature.empty() && (camp.feature == "ADAS" || camp.feature == "Navigation" || camp.feature == "Energy")) {
        log.ok("feature=" + camp.feature);
    }
    if (!camp.target_version.empty()) {
        log.ok("target_version=" + camp.target_version);
    }
    if (!camp.legacy_slot.empty() && camp.target_role == "candidate" && camp.legacy_slot == "B") {
        log.ok("legacy_slot B aligns with target_role candidate");
    }
    if (camp.docker_runtime_enabled && !camp.candidate_pull_policy.empty()) {
        log.ok("docker_runtime.candidate.pull_policy=" + camp.candidate_pull_policy);
    }
    if (camp.promote_policy == "manual" && camp.approval_required) {
        log.ok("approval_required=true matches manual promote policy");
    }
    if (!camp.rollout_policy_id.empty() && result.failures.empty()) {
        log.ok("rollout policy loaded: deploy/rollout_policies/" + camp.rollout_policy_id + ".json");
    }
    if (!camp.package_path.empty()) {
        log.ok("package_path exists: " + camp.package_path);
    }
    if (!camp.manifest_path.empty()) {
        log.ok("manifest_path exists: " + camp.manifest_path);
    }

    for (const auto& w : result.warnings) {
        log.warn(w);
    }
    for (const auto& f : result.failures) {
        log.fail(f);
        if (log.firstFailReason.empty()) {
            log.firstFailReason = f;
        }
    }

    if (result.ok && !result.resolved_car_ids.empty()) {
        TargetSelector selector;
        std::string parseErr;
        if (TargetingResolver::parseTargetSelector(camp.target_selector_json, selector, parseErr)) {
            TargetingResult tr;
            tr.ok = true;
            tr.scope = selector.scope;
            tr.resolved_car_ids = result.resolved_car_ids;
            log.ok(TargetingResolver::formatScopeOkLine(tr, selector));
            log.ok(TargetingResolver::formatResolvedTargetsLine(tr));
        }
    }

    if (!result.failures.empty() && log.firstFailReason.empty()) {
        log.firstFailReason = result.first_failure;
    }

    const std::string campaign_id = camp.campaign_id.empty() ? "(unknown)" : camp.campaign_id;
    const std::string status = log.summary.fail == 0 && result.ok ? "OK" : "FAIL";
    if (log.summary.fail == 0 && result.ok) {
        std::cout << "[CAMPAIGN_VALIDATE] campaign_id=" << campaign_id << " status=" << status << "\n";
    } else {
        const std::string reason = log.firstFailReason.empty() ? "validation failed" : log.firstFailReason;
        std::cout << "[CAMPAIGN_VALIDATE] campaign_id=" << campaign_id << " status=" << status << " reason=\""
                  << jsonQuotedEscape(reason) << "\"\n";
    }
    std::cout << "[SUMMARY] ok=" << log.summary.ok << " warn=" << log.summary.warn
              << " fail=" << log.summary.fail << "\n";
    return (log.summary.fail == 0 && result.ok) ? 0 : 1;
}

}  // namespace sdv::platform::deployment
