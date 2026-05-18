#include "DeploymentManager/CampaignRunManager.h"

#include "DeploymentManager/CampaignRunRecorder.h"
#include "DeploymentManager/CampaignValidation.h"
#include "DeploymentManager/TargetingResolver.h"

#include <iostream>
#include <sstream>

namespace sdv::platform::deployment {

namespace {

void printRunLine(const std::string& line) {
    std::cout << "[CAMPAIGN_RUN] " << line << "\n";
}

void printRunFail(const std::string& reason) {
    std::cerr << "[CAMPAIGN_RUN] status=FAIL reason=\"" << reason << "\"\n";
}

std::string joinCars(const std::vector<std::string>& cars) {
    std::ostringstream os;
    for (std::size_t i = 0; i < cars.size(); ++i) {
        if (i) {
            os << ",";
        }
        os << cars[i];
    }
    return os.str();
}

void fillRecordFromCampaign(CampaignRunRecord& record, const CampaignDocument& doc, const std::string& phase,
    const std::string& campaignJsonPath) {
    record.campaign_id = doc.campaign_id;
    record.campaign_file = campaignJsonPath;
    record.rollout_policy_id = doc.rollout_policy_id;
    record.phase = phase;
    record.feature = doc.feature;
    record.target_version = doc.target_version;
    record.legacy_slot = fleetDeploySlotForCampaign(doc);
    record.docker_requested = doc.docker_runtime_enabled;
    record.docker_image = doc.docker_image;

    if (phase == "promote") {
        record.target_role = "primary";
        record.docker_mode = "promote";
        record.docker_pull_requested = false;
        record.docker_pull_policy.clear();
    } else {
        record.target_role = doc.target_role;
        record.docker_mode = "candidate";
        record.docker_pull_requested = !doc.candidate_pull_policy.empty();
        record.docker_pull_policy = doc.candidate_pull_policy;
    }
}

std::string aggregateStatus(int success, int failed) {
    if (failed == 0 && success > 0) {
        return "success";
    }
    if (success == 0 && failed > 0) {
        return "failed";
    }
    if (success > 0 && failed > 0) {
        return "partial";
    }
    return "failed";
}

bool saveAndReportRun(const std::string& workDir, CampaignRunRecord& record, bool deploy_succeeded) {
    const CampaignRunSaveResult saved = CampaignRunRecorder::saveRunRecord(workDir, record);
    if (saved.ok) {
        printRunLine("run_record_saved " + saved.relative_path);
        return true;
    }
    std::cerr << "[CAMPAIGN_RUN] run_record_error=\"" << saved.error << "\"\n";
    if (deploy_succeeded) {
        printRunLine("status=PARTIAL deploy_succeeded=true run_record_saved=false");
    }
    return false;
}

DeploymentManager::FleetDockerRuntimeOptions dockerOptionsForPhase(const CampaignDocument& doc,
    const std::string& phase) {
    DeploymentManager::FleetDockerRuntimeOptions docker;
    docker.requested = doc.docker_runtime_enabled;
    if (phase == "promote") {
        docker.mode = "promote";
        docker.pull_policy.clear();
    } else {
        docker.mode = "candidate";
        docker.pull_policy = doc.candidate_pull_policy;
    }
    return docker;
}

std::string mappedRoleLabel(const std::string& phase) {
    return phase == "promote" ? "primary_from_candidate" : "candidate";
}

std::string executionFailureMessage(const std::string& phase, const std::string& car_id) {
    return phase == "promote" ? ("primary promote failed for " + car_id)
                              : ("candidate execution failed for " + car_id);
}

}  // namespace

CampaignRunManager::CampaignRunManager(std::string workDir)
    : workDir_(std::move(workDir)) {}

int CampaignRunManager::runCampaignFile(const std::string& campaignJsonPath, const std::string& phase,
    DeploymentManager& deployment) {
    if (phase.empty()) {
        printRunFail("missing --phase (supported: candidate, promote)");
        return 1;
    }
    if (phase != "candidate" && phase != "promote") {
        printRunFail("unknown phase: " + phase);
        return 1;
    }

    const std::string started_at = CampaignRunRecorder::currentIsoTimestamp();
    const std::string compact_ts = CampaignRunRecorder::compactTimestampForId();

    CampaignValidateOptions validateOpts;
    validateOpts.require_docker_runtime = true;
    validateOpts.runtime_preconditions_as_warnings = true;
    validateOpts.run_phase = phase;

    const CampaignValidationResult preflight = validateCampaignFile(workDir_, campaignJsonPath, validateOpts);

    auto writePreflightFailureRecord = [&]() -> bool {
        if (preflight.document.campaign_id.empty()) {
            return false;
        }
        CampaignRunRecord record;
        record.run_id = CampaignRunRecorder::buildRunId(compact_ts, preflight.document.campaign_id, phase);
        record.started_at = started_at;
        record.finished_at = CampaignRunRecorder::currentIsoTimestamp();
        fillRecordFromCampaign(record, preflight.document, phase, campaignJsonPath);
        record.resolved_targets = preflight.resolved_car_ids;
        record.warnings = preflight.warnings;
        record.failures = preflight.failures;
        if (record.failures.empty() && !preflight.first_failure.empty()) {
            record.failures.push_back(preflight.first_failure);
        }
        record.aggregate_total = static_cast<int>(preflight.resolved_car_ids.size());
        record.aggregate_success = 0;
        record.aggregate_failed = record.aggregate_total > 0 ? record.aggregate_total : 1;
        record.status = "failed";
        saveAndReportRun(workDir_, record, false);
        return true;
    };

    if (!preflight.ok) {
        printRunLine("campaign_id=" + (preflight.document.campaign_id.empty() ? std::string("(unknown)")
                                                                              : preflight.document.campaign_id)
                       + " phase=" + phase);
        printRunFail(preflight.first_failure.empty() ? "campaign validation failed" : preflight.first_failure);
        for (const auto& f : preflight.failures) {
            std::cerr << "[CAMPAIGN_RUN] validation_error: " << f << "\n";
        }
        writePreflightFailureRecord();
        return 1;
    }

    const CampaignDocument& doc = preflight.document;

    if (phase == "candidate" && doc.target_role != "candidate") {
        printRunFail("campaign-run --phase candidate requires target_role=candidate");
        CampaignRunRecord record;
        record.run_id = CampaignRunRecorder::buildRunId(compact_ts, doc.campaign_id, phase);
        record.started_at = started_at;
        record.finished_at = CampaignRunRecorder::currentIsoTimestamp();
        fillRecordFromCampaign(record, doc, phase, campaignJsonPath);
        record.resolved_targets = preflight.resolved_car_ids;
        record.warnings = preflight.warnings;
        record.failures = {"target_role must be candidate for --phase candidate"};
        record.status = "failed";
        record.aggregate_total = 0;
        record.aggregate_failed = 1;
        saveAndReportRun(workDir_, record, false);
        return 1;
    }

    CampaignRunRecord record;
    record.run_id = CampaignRunRecorder::buildRunId(compact_ts, doc.campaign_id, phase);
    record.started_at = started_at;
    fillRecordFromCampaign(record, doc, phase, campaignJsonPath);
    record.resolved_targets = preflight.resolved_car_ids;
    record.warnings = preflight.warnings;

    printRunLine("campaign_id=" + doc.campaign_id + " phase=" + phase);
    printRunLine("rollout_policy=" + doc.rollout_policy_id);

    for (const auto& w : preflight.warnings) {
        std::cout << "[CAMPAIGN_RUN] WARN " << w << "\n";
    }

    printRunLine("resolved_targets count=" + std::to_string(preflight.resolved_car_ids.size()) + " cars="
                 + joinCars(preflight.resolved_car_ids));

    const std::string slot = fleetDeploySlotForCampaign(doc);
    const bool sync_config = doc.sync_config;
    const bool defer_native_until_restart = !doc.no_defer_native;
    const DeploymentManager::FleetDockerRuntimeOptions docker = dockerOptionsForPhase(doc, phase);

    std::ostringstream mapped;
    mapped << "mapped_to feature=" << doc.feature << " role=" << mappedRoleLabel(phase) << " legacy_slot=" << slot
           << " version=" << doc.target_version << " docker_runtime=" << docker.mode;
    if (phase == "candidate" && !doc.candidate_pull_policy.empty()) {
        mapped << " pull_policy=" << doc.candidate_pull_policy;
    }
    printRunLine(mapped.str());

    printRunLine("execution=started feature=" + doc.feature + " version=" + doc.target_version + " phase=" + phase);

    int success = 0;
    int failed = 0;

    for (const auto& car_id : preflight.resolved_car_ids) {
        const DeploymentManager::DeploySummary perCar = deployment.deployToVehicleWithSummary(
            car_id, doc.feature, slot, doc.target_version, sync_config, defer_native_until_restart, &docker);

        deployment.printFleetDeploySummary(perCar);

        CampaignRunVehicleRecord row = CampaignRunRecorder::vehicleRecordFromDeploy(car_id, doc, slot, phase, perCar);
        record.per_vehicle.push_back(row);

        const bool carOk = row.result == "success";
        if (carOk) {
            ++success;
        } else {
            ++failed;
            if (!row.reason.empty()) {
                record.failures.push_back(car_id + ": " + row.reason);
            } else {
                record.failures.push_back(executionFailureMessage(phase, car_id));
            }
        }

        std::ostringstream targetLine;
        targetLine << "target=" << car_id << " result=" << row.result;
        if (!row.docker_runtime_result.empty()) {
            targetLine << " docker_runtime_result=" << row.docker_runtime_result;
        }
        if (!row.reason.empty() && !carOk) {
            targetLine << " reason=" << row.reason;
        }
        printRunLine(targetLine.str());
    }

    record.aggregate_total = static_cast<int>(preflight.resolved_car_ids.size());
    record.aggregate_success = success;
    record.aggregate_failed = failed;
    record.finished_at = CampaignRunRecorder::currentIsoTimestamp();
    record.status = aggregateStatus(success, failed);

    if (record.aggregate_total == 0) {
        printRunFail("target_selector resolved no vehicles");
        record.failures.push_back("target_selector resolved no vehicles");
        record.status = "failed";
        record.aggregate_failed = 1;
        saveAndReportRun(workDir_, record, false);
        return 1;
    }

    const bool deploy_ok = (failed == 0 && success > 0);
    saveAndReportRun(workDir_, record, deploy_ok);

    if (deploy_ok) {
        printRunLine("status=SUCCESS total=" + std::to_string(record.aggregate_total) + " success="
                       + std::to_string(success) + " failed=" + std::to_string(failed));
        return 0;
    }

    printRunFail(failed > 0 && !record.failures.empty() ? record.failures.back()
                                                        : executionFailureMessage(phase, "fleet"));
    printRunLine("status=FAIL total=" + std::to_string(record.aggregate_total) + " success="
                   + std::to_string(success) + " failed=" + std::to_string(failed));
    return 1;
}

}  // namespace sdv::platform::deployment
