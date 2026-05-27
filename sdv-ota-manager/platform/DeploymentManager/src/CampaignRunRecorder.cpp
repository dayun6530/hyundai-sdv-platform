#include "DeploymentManager/CampaignRunRecorder.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace sdv::platform::deployment {

namespace {

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

void appendStringArray(std::ostringstream& os, const std::vector<std::string>& items) {
    os << "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) {
            os << ",";
        }
        os << "\"" << jsonEscape(items[i]) << "\"";
    }
    os << "]";
}

std::tm localTmNow() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

}  // namespace

std::string CampaignRunRecorder::currentIsoTimestamp() {
    const std::tm tm = localTmNow();
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

std::string CampaignRunRecorder::compactTimestampForId() {
    const std::tm tm = localTmNow();
    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return os.str();
}

std::string CampaignRunRecorder::sanitizePathToken(const std::string& token) {
    std::string out;
    out.reserve(token.size());
    for (char c : token) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? "unknown" : out;
}

std::string CampaignRunRecorder::buildRunId(const std::string& compactTs, const std::string& campaign_id,
    const std::string& phase) {
    return "run_" + compactTs + "_" + sanitizePathToken(campaign_id) + "_" + sanitizePathToken(phase);
}

std::string CampaignRunRecorder::buildFleetDeployCommand(const CampaignDocument& doc, const std::string& car_id,
    const std::string& slot, const std::string& phase) {
    std::ostringstream os;
    os << "fleet-deploy vehicle " << car_id << " " << doc.feature << " " << slot << " " << doc.target_version;
    if (doc.sync_config) {
        os << " --sync-config";
    }
    if (doc.no_defer_native) {
        os << " --no-defer-native";
    }
    if (doc.docker_runtime_enabled) {
        if (phase == "promote") {
            os << " --docker-runtime=promote";
        } else {
            os << " --docker-runtime=candidate";
            if (!doc.candidate_pull_policy.empty()) {
                os << " --docker-pull=" << doc.candidate_pull_policy;
            }
        }
    }
    return os.str();
}

CampaignRunVehicleRecord CampaignRunRecorder::vehicleRecordFromDeploy(const std::string& car_id,
    const CampaignDocument& doc,
    const std::string& slot,
    const std::string& phase,
    const DeploymentManager::DeploySummary& summary) {
    CampaignRunVehicleRecord row;
    row.car_id = car_id;
    row.deploy_id = summary.deploy_id;
    row.history_path = "deploy/history/fleet_history.jsonl";
    row.state_file = "deploy/state/" + car_id + ".json";
    row.config_file = "config/car/" + car_id + ".conf";
    row.fleet_deploy_command = buildFleetDeployCommand(doc, car_id, slot, phase);

    if (!summary.results.empty()) {
        const auto& r = summary.results.front();
        const bool dockerFailed =
            r.docker_runtime_requested && r.docker_runtime_result == "failed";
        row.result = (r.success && !dockerFailed) ? "success" : "failed";
        row.reason = r.reason;
        if (dockerFailed && row.reason.empty()) {
            row.reason = r.docker_runtime_reason.empty() ? "docker runtime failed" : r.docker_runtime_reason;
        }
        row.state_updated = r.state_updated;
        row.config_synced = r.config_synced;
        row.docker_runtime_result = r.docker_runtime_result;
        row.docker_runtime_mode = r.docker_runtime_mode;
        row.docker_runtime_action = r.docker_runtime_action;
        row.docker_runtime_image = r.docker_runtime_image;
        row.docker_runtime_container = r.docker_runtime_container;
        if (row.deploy_id.empty()) {
            row.deploy_id = summary.deploy_id;
        }
    } else {
        row.result = "failed";
        row.reason = "no deploy result";
    }
    return row;
}

CampaignRunSaveResult CampaignRunRecorder::saveRunRecord(const std::string& workDir, const CampaignRunRecord& record) {
    CampaignRunSaveResult out;
    out.run_id = record.run_id;

    const std::string dir = workDir + "/deploy/campaign_runs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::string filename = sanitizePathToken(record.run_id) + ".json";
    out.relative_path = "deploy/campaign_runs/" + filename;
    const std::string absPath = workDir + "/" + out.relative_path;

    std::ostringstream os;
    os << "{\n";
    os << "  \"schema_version\": " << record.schema_version << ",\n";
    os << "  \"run_id\": \"" << jsonEscape(record.run_id) << "\",\n";
    os << "  \"campaign_id\": \"" << jsonEscape(record.campaign_id) << "\",\n";
    os << "  \"campaign_file\": \"" << jsonEscape(record.campaign_file) << "\",\n";
    os << "  \"rollout_policy_id\": \"" << jsonEscape(record.rollout_policy_id) << "\",\n";
    os << "  \"phase\": \"" << jsonEscape(record.phase) << "\",\n";
    os << "  \"status\": \"" << jsonEscape(record.status) << "\",\n";
    os << "  \"started_at\": \"" << jsonEscape(record.started_at) << "\",\n";
    os << "  \"finished_at\": \"" << jsonEscape(record.finished_at) << "\",\n";
    os << "  \"feature\": \"" << jsonEscape(record.feature) << "\",\n";
    os << "  \"target_version\": \"" << jsonEscape(record.target_version) << "\",\n";
    os << "  \"target_role\": \"" << jsonEscape(record.target_role) << "\",\n";
    os << "  \"legacy_slot\": \"" << jsonEscape(record.legacy_slot) << "\",\n";
    os << "  \"docker_runtime\": {\n";
    os << "    \"requested\": " << (record.docker_requested ? "true" : "false") << ",\n";
    os << "    \"mode\": \"" << jsonEscape(record.docker_mode) << "\",\n";
    os << "    \"pull_requested\": " << (record.docker_pull_requested ? "true" : "false") << ",\n";
    os << "    \"pull_policy\": \"" << jsonEscape(record.docker_pull_policy) << "\",\n";
    os << "    \"image\": \"" << jsonEscape(record.docker_image) << "\"\n";
    os << "  },\n";
    os << "  \"resolved_targets\": ";
    appendStringArray(os, record.resolved_targets);
    os << ",\n";
    os << "  \"aggregate\": {\n";
    os << "    \"total\": " << record.aggregate_total << ",\n";
    os << "    \"success\": " << record.aggregate_success << ",\n";
    os << "    \"failed\": " << record.aggregate_failed << "\n";
    os << "  },\n";
    os << "  \"per_vehicle\": [\n";
    for (std::size_t i = 0; i < record.per_vehicle.size(); ++i) {
        const auto& v = record.per_vehicle[i];
        if (i) {
            os << ",\n";
        }
        os << "    {\n";
        os << "      \"car_id\": \"" << jsonEscape(v.car_id) << "\",\n";
        os << "      \"result\": \"" << jsonEscape(v.result) << "\",\n";
        os << "      \"reason\": \"" << jsonEscape(v.reason) << "\",\n";
        os << "      \"deploy_id\": \"" << jsonEscape(v.deploy_id) << "\",\n";
        os << "      \"state_updated\": " << (v.state_updated ? "true" : "false") << ",\n";
        os << "      \"config_synced\": " << (v.config_synced ? "true" : "false") << ",\n";
        os << "      \"docker_runtime_result\": \"" << jsonEscape(v.docker_runtime_result) << "\",\n";
        os << "      \"docker_runtime_mode\": \"" << jsonEscape(v.docker_runtime_mode) << "\",\n";
        os << "      \"docker_runtime_action\": \"" << jsonEscape(v.docker_runtime_action) << "\",\n";
        os << "      \"docker_runtime_image\": \"" << jsonEscape(v.docker_runtime_image) << "\",\n";
        os << "      \"docker_runtime_container\": \"" << jsonEscape(v.docker_runtime_container) << "\",\n";
        os << "      \"state_file\": \"" << jsonEscape(v.state_file) << "\",\n";
        os << "      \"config_file\": \"" << jsonEscape(v.config_file) << "\",\n";
        os << "      \"history_path\": \"" << jsonEscape(v.history_path) << "\",\n";
        os << "      \"fleet_deploy_command\": \"" << jsonEscape(v.fleet_deploy_command) << "\"\n";
        os << "    }";
    }
    os << "\n  ],\n";
    os << "  \"warnings\": ";
    appendStringArray(os, record.warnings);
    os << ",\n";
    os << "  \"failures\": ";
    appendStringArray(os, record.failures);
    os << "\n}\n";

    std::ofstream file(absPath);
    if (!file) {
        out.error = "failed to open for write: " + out.relative_path;
        return out;
    }
    file << os.str();
    if (!file.good()) {
        out.error = "failed to write: " + out.relative_path;
        return out;
    }
    out.ok = true;
    return out;
}

}  // namespace sdv::platform::deployment
