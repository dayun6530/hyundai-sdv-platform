#include "DeploymentManager/CampaignDocument.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

namespace sdv::platform::deployment {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
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

std::string extractNestedObject(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return {};
    }
    std::size_t i = json.find('{', keyPos);
    if (i == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (std::size_t j = i; j < json.size(); ++j) {
        if (json[j] == '{') {
            ++depth;
        } else if (json[j] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(i, j - i + 1);
            }
        }
    }
    return {};
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

std::string readFileText(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

CampaignLoadResult loadCampaignFromFile(const std::string& workDir, const std::string& campaignJsonPath) {
    CampaignLoadResult out;
    const std::string absCampaign = resolvePlatformPath(workDir, campaignJsonPath);
    if (!pathExists(absCampaign)) {
        out.error = "campaign file not found: " + campaignJsonPath;
        return out;
    }
    const std::string json = readFileText(absCampaign);
    if (json.empty()) {
        out.error = "empty or unreadable campaign file: " + campaignJsonPath;
        return out;
    }

    CampaignDocument& doc = out.document;
    doc.campaign_id = jsonStringField(json, "campaign_id").value_or("");
    doc.feature = jsonStringField(json, "feature").value_or("");
    doc.target_version = jsonStringField(json, "target_version").value_or("");
    doc.target_role = toLower(jsonStringField(json, "target_role").value_or(""));
    doc.legacy_slot = jsonStringField(json, "legacy_slot").value_or("");
    doc.package_id = jsonStringField(json, "package_id").value_or("");
    doc.package_path = jsonStringField(json, "package_path").value_or("");
    doc.manifest_path = jsonStringField(json, "manifest_path").value_or("");
    doc.docker_image = jsonStringField(json, "docker_image").value_or("");
    doc.artifact_type = jsonStringField(json, "artifact_type").value_or("");
    doc.rollout_policy_id = jsonStringField(json, "rollout_policy_id").value_or("");
    doc.target_selector_json = extractNestedObject(json, "target_selector");
    doc.safety_json = extractNestedObject(json, "safety_requirements");
    doc.execution_json = extractNestedObject(json, "execution_options");
    doc.docker_runtime_json = extractNestedObject(json, "docker_runtime");
    if (const auto ap = jsonBoolField(json, "approval_required")) {
        doc.approval_required = *ap;
    }
    if (const auto sc = jsonBoolField(doc.execution_json, "sync_config")) {
        doc.sync_config = *sc;
    }
    if (const auto nd = jsonBoolField(doc.execution_json, "no_defer_native")) {
        doc.no_defer_native = *nd;
    }
    if (const auto en = jsonBoolField(doc.docker_runtime_json, "enabled")) {
        doc.docker_runtime_enabled = *en;
    }
    const std::string candidateJson = extractNestedObject(doc.docker_runtime_json, "candidate");
    doc.candidate_pull_policy = toLower(jsonStringField(candidateJson, "pull_policy").value_or(""));
    const std::string promoteJson = extractNestedObject(doc.docker_runtime_json, "promote");
    doc.promote_mode = toLower(jsonStringField(promoteJson, "mode").value_or(""));
    doc.promote_policy = toLower(jsonStringField(promoteJson, "policy").value_or(""));
    if (const auto sr = jsonBoolField(promoteJson, "separate_run_required")) {
        doc.promote_separate_run = *sr;
    }

    out.ok = true;
    return out;
}

std::string fleetDeploySlotForCampaign(const CampaignDocument& doc) {
    if (!doc.legacy_slot.empty()) {
        return doc.legacy_slot;
    }
    if (doc.target_role == "candidate") {
        return "B";
    }
    if (doc.target_role == "primary") {
        return "A";
    }
    return "B";
}

}  // namespace sdv::platform::deployment
