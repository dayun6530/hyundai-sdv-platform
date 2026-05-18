#include "DeploymentManager/CampaignValidation.h"

#include "DeploymentManager/StateManager.h"
#include "DeploymentManager/VehicleRegistry.h"
#include "DeploymentManager/TargetingResolver.h"
#include "DeploymentManager/VersionManager.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

namespace sdv::platform::deployment {

namespace {

void addFailure(CampaignValidationResult& result, const std::string& msg) {
    result.failures.push_back(msg);
    if (result.first_failure.empty()) {
        result.first_failure = msg;
    }
}

void addWarning(CampaignValidationResult& result, const std::string& msg) {
    result.warnings.push_back(msg);
}

bool isSupportedFeature(const std::string& feature) {
    return feature == "ADAS" || feature == "Navigation" || feature == "Energy";
}

std::string resolvePlatformPath(const std::string& platformRoot, const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p.lexically_normal().generic_string();
    }
    return (std::filesystem::path(platformRoot) / p).lexically_normal().generic_string();
}

bool pathExists(const std::string& absPath) {
    std::error_code ec;
    return std::filesystem::exists(absPath, ec) && !ec;
}

bool isDir(const std::string& absPath) {
    std::error_code ec;
    return std::filesystem::is_directory(absPath, ec) && !ec;
}

std::string readFileText(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::optional<std::string> jsonStringField(const std::string& json, const std::string& key) {
    try {
        const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        if (std::regex_search(json, m, re) && m.size() >= 2) {
            return m[1].str();
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<bool> jsonBoolField(const std::string& json, const std::string& key) {
    try {
        const std::regex reTrue("\"" + key + "\"\\s*:\\s*true");
        const std::regex reFalse("\"" + key + "\"\\s*:\\s*false");
        if (std::regex_search(json, reTrue)) {
            return true;
        }
        if (std::regex_search(json, reFalse)) {
            return false;
        }
    } catch (...) {
    }
    return std::nullopt;
}

bool policyHasStagePattern(const std::string& policyJson, const std::string& pattern) {
    return policyJson.find(pattern) != std::string::npos;
}

}  // namespace

CampaignValidationResult validateCampaign(const std::string& workDir, const CampaignDocument& camp,
    const CampaignValidateOptions& options) {
    CampaignValidationResult result;
    result.document = camp;

    if (camp.campaign_id.empty()) {
        addFailure(result, "missing required field: campaign_id");
    }
    if (camp.feature.empty()) {
        addFailure(result, "missing required field: feature");
    } else if (!isSupportedFeature(camp.feature)) {
        addFailure(result, "unsupported feature: " + camp.feature);
    }
    if (camp.target_version.empty()) {
        addFailure(result, "missing required field: target_version");
    }
    if (camp.target_role.empty()) {
        addFailure(result, "missing required field: target_role");
    } else if (camp.target_role != "candidate" && camp.target_role != "primary") {
        addFailure(result, "invalid target_role: " + camp.target_role);
    } else if (options.run_phase == "candidate" && camp.docker_runtime_enabled && camp.target_role != "candidate") {
        addFailure(result, "target_role must be candidate for campaign-run --phase candidate");
    } else if (options.run_phase == "promote" && camp.target_role != "candidate") {
        addFailure(result, "promote phase expects campaign target_role=candidate (artifact under validation)");
    }
    if (camp.rollout_policy_id.empty()) {
        addFailure(result, "missing required field: rollout_policy_id");
    }
    if (camp.package_path.empty() && camp.manifest_path.empty()) {
        addFailure(result, "missing required field: package_path or manifest_path");
    }

    if (!camp.legacy_slot.empty()) {
        if (camp.legacy_slot == "B" && camp.target_role == "primary") {
            addFailure(result, "legacy_slot B conflicts with target_role primary");
        } else if (camp.legacy_slot == "A" && camp.target_role == "candidate") {
            addFailure(result, "legacy_slot A conflicts with target_role candidate");
        } else if (camp.legacy_slot != "A" && camp.legacy_slot != "B") {
            addFailure(result, "invalid legacy_slot: " + camp.legacy_slot);
        }
    }

    if (camp.artifact_type == "docker_image" && camp.docker_image.empty()) {
        addFailure(result, "missing required field: docker_image");
    }
    const bool isPromoteRun = (options.run_phase == "promote");
    const bool isCandidateRun = (options.run_phase == "candidate");

    if (options.require_docker_runtime) {
        if (!camp.docker_runtime_enabled) {
            addFailure(result, "docker_runtime.enabled must be true for campaign-run");
        } else if (camp.docker_image.empty()) {
            addFailure(result, "missing required field: docker_image (docker_runtime.enabled=true)");
        } else if (isCandidateRun) {
            if (camp.candidate_pull_policy.empty()) {
                addFailure(result, "missing docker_runtime.candidate.pull_policy");
            } else if (camp.candidate_pull_policy != "missing" && camp.candidate_pull_policy != "always") {
                addFailure(result, "invalid docker_runtime.candidate.pull_policy: " + camp.candidate_pull_policy);
            }
        } else if (isPromoteRun) {
            if (camp.promote_mode != "promote") {
                addFailure(result, "docker_runtime.promote.mode must be promote for campaign-run --phase promote");
            }
        } else if (camp.candidate_pull_policy.empty()) {
            addFailure(result, "missing docker_runtime.candidate.pull_policy");
        }
    }

    if (camp.promote_policy == "manual" && !camp.approval_required) {
        addFailure(result, "approval_required must be true when docker_runtime.promote.policy is manual");
    }

    if (!camp.rollout_policy_id.empty()) {
        const std::string policyRel = "deploy/rollout_policies/" + camp.rollout_policy_id + ".json";
        const std::string policyAbs = resolvePlatformPath(workDir, policyRel);
        if (!pathExists(policyAbs)) {
            addFailure(result, "rollout policy not found: " + policyRel);
        } else {
            const std::string policyJson = readFileText(policyAbs);
            if (policyJson.empty()) {
                addFailure(result, "rollout policy empty or unreadable: " + policyRel);
            } else {
                const std::string policy_id = jsonStringField(policyJson, "policy_id").value_or("");
                if (policy_id != camp.rollout_policy_id) {
                    addFailure(result, "rollout policy_id mismatch");
                }
                const bool hasCandidate = policyHasStagePattern(policyJson, "\"deploy_action\": \"candidate\"")
                    || policyHasStagePattern(policyJson, "\"deploy_action\":\"candidate\"");
                const bool hasPromote = policyHasStagePattern(policyJson, "\"deploy_action\": \"promote\"")
                    || policyHasStagePattern(policyJson, "\"deploy_action\":\"promote\"");
                if (isCandidateRun && !hasCandidate) {
                    addFailure(result, "rollout policy missing candidate deploy stage");
                }
                if (isPromoteRun && !hasPromote) {
                    addFailure(result, "rollout policy missing primary promote stage");
                }
                if (options.run_phase.empty() && !hasCandidate) {
                    addFailure(result, "rollout policy missing candidate deploy stage");
                }
            }
        }
    }

    std::string manifestRel = camp.manifest_path;
    if (!camp.package_path.empty()) {
        const std::string pkgAbs = resolvePlatformPath(workDir, camp.package_path);
        if (!pathExists(pkgAbs)) {
            addFailure(result, "package_path does not exist: " + camp.package_path);
        } else if (!isDir(pkgAbs)) {
            addFailure(result, "package_path is not a directory: " + camp.package_path);
        }
        if (manifestRel.empty()) {
            manifestRel = camp.package_path;
            if (manifestRel.back() != '/') {
                manifestRel += "/";
            }
            manifestRel += "manifest.json";
        }
    }
    if (!manifestRel.empty()) {
        const std::string manifestAbs = resolvePlatformPath(workDir, manifestRel);
        if (!pathExists(manifestAbs)) {
            addFailure(result, "manifest_path does not exist: " + manifestRel);
        } else {
            const std::string manifestJson = readFileText(manifestAbs);
            if (manifestJson.empty()) {
                addFailure(result, "manifest unreadable: " + manifestRel);
            } else {
                const std::string mDocker = jsonStringField(manifestJson, "docker_image").value_or("");
                const std::string mArtifact = jsonStringField(manifestJson, "artifact_type").value_or("");
                if (!camp.docker_image.empty() && !mDocker.empty() && mDocker != camp.docker_image) {
                    addFailure(result, "docker_image mismatch: campaign vs manifest");
                }
                if (!camp.artifact_type.empty() && !mArtifact.empty() && mArtifact != camp.artifact_type) {
                    addFailure(result, "artifact_type mismatch: campaign vs manifest");
                }
            }
        }
    } else if (camp.package_path.empty()) {
        addFailure(result, "cannot resolve manifest without package_path or manifest_path");
    }

    std::string vmErr;
    const auto vmOpt = VersionManager::loadFromFile(workDir + "/deploy/version_map.json", vmErr);
    if (!vmOpt) {
        addFailure(result, vmErr.empty() ? "failed to load deploy/version_map.json" : vmErr);
    } else {
        std::string slotErr;
        const auto bInfo = vmOpt->getInfo(camp.feature, 'B', slotErr);
        if (!bInfo) {
            addFailure(result, "version_map missing candidate mapping for " + camp.feature);
        } else {
            if (bInfo->version != camp.target_version) {
                addFailure(result, "version_map candidate version mismatch");
            }
            if (!camp.package_path.empty() && !bInfo->package_path.empty()
                && bInfo->package_path != camp.package_path) {
                addFailure(result, "version_map candidate package_path mismatch");
            }
        }
    }

    std::string registryErr;
    const auto registryOpt = VehicleRegistry::loadFromFile(workDir + "/config/fleet/vehicles.json", registryErr);
    if (!registryOpt) {
        addFailure(result, registryErr.empty() ? "failed to load vehicles.json" : registryErr);
    } else if (camp.target_selector_json.empty()) {
        addFailure(result, "missing target_selector object");
    } else {
        TargetSelector selector;
        std::string selectorParseErr;
        if (!TargetingResolver::parseTargetSelector(camp.target_selector_json, selector, selectorParseErr)) {
            addFailure(result, selectorParseErr.empty() ? "invalid target_selector" : selectorParseErr);
        } else {
            TargetingCompatibilityFilters extraFilters;
            const TargetingResult targeting = TargetingResolver::resolve(selector, *registryOpt, &extraFilters);
            for (const auto& w : targeting.warnings) {
                addWarning(result, w);
            }
            for (const auto& f : targeting.failures) {
                addFailure(result, f);
            }
            if (targeting.ok) {
                result.resolved_car_ids = targeting.resolved_car_ids;
            }
        }
    }

    const bool respectKill = jsonBoolField(camp.safety_json, "respect_kill_switch").value_or(false);
    const bool requireOtaReady = jsonBoolField(camp.safety_json, "require_ota_ready").value_or(false);
    const bool requireDriver = jsonBoolField(camp.safety_json, "require_driver_approval").value_or(false);

    if (respectKill || requireOtaReady || requireDriver) {
        StateManager sm(workDir);
        for (const auto& car_id : result.resolved_car_ids) {
            std::string stErr;
            const auto stateOpt = sm.loadState(car_id, stErr);
            if (!stateOpt) {
                addWarning(result, "cannot load state for safety check: " + car_id);
                continue;
            }
            const StateManager::FeatureState* fs = StateManager::featureRef(*stateOpt, camp.feature);
            if (fs == nullptr) {
                addWarning(result, "feature " + camp.feature + " not found in state for " + car_id);
                continue;
            }
            if (respectKill && fs->kill_switch) {
                const std::string msg = "kill_switch is active for " + car_id;
                if (options.runtime_preconditions_as_warnings) {
                    addWarning(result, msg);
                } else {
                    addWarning(result, msg);
                }
            }
            if (requireOtaReady) {
                const std::string mode = stateOpt->ota.update_mode;
                if (mode != "READY") {
                    const std::string msg =
                        "update_mode is not READY for " + car_id + " (current=" + mode + ")";
                    if (options.runtime_preconditions_as_warnings) {
                        addWarning(result, msg);
                    } else {
                        addWarning(result, msg);
                    }
                }
            }
            if (requireDriver && !stateOpt->ota.driver_approved) {
                addWarning(result, "driver_approved=false for " + car_id);
            }
        }
    }

    result.ok = result.failures.empty() && !result.resolved_car_ids.empty();
    if (result.failures.empty() && result.resolved_car_ids.empty()) {
        addFailure(result, "target_selector resolved no vehicles");
        result.ok = false;
    }
    return result;
}

CampaignValidationResult validateCampaignFile(const std::string& workDir, const std::string& campaignJsonPath,
    const CampaignValidateOptions& options) {
    CampaignValidationResult result;
    const CampaignLoadResult loaded = loadCampaignFromFile(workDir, campaignJsonPath);
    if (!loaded.ok) {
        addFailure(result, loaded.error);
        result.ok = false;
        return result;
    }
    return validateCampaign(workDir, loaded.document, options);
}

}  // namespace sdv::platform::deployment
