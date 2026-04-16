#include "DeploymentManager/DeploymentManager.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <sstream>
#include <vector>

namespace sdv::platform::deployment {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::string toUpperDomain(std::string d) {
    for (char& c : d) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return d;
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string hexU64(std::uint64_t v) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << v;
    return toLower(os.str());
}

std::string nowTimestampIso() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return ts.str();
}

const std::array<const char*, 3> kInventoryDomains = {"ADAS", "NAV", "ENERGY"};

}  // namespace

DeploymentManager::DeploymentManager(std::string workingDirectory)
    : history_(workingDirectory + "/deploy/history")
    , workDir_(std::move(workingDirectory))
    , packagesDir_(workDir_ + "/deploy/packages")
    , stagingDir_(workDir_ + "/deploy/staging")
    , activeDir_(workDir_ + "/deploy/active")
    , backupDir_(workDir_ + "/deploy/backup")
    , metadataDir_(workDir_ + "/deploy/metadata")
    , selectionPath_(workDir_ + "/deploy/active_selection.conf")
    , stacksDir_(workDir_ + "/deploy/stacks")
    , manifestPath_(workDir_ + "/deploy/manifest.txt") {
    if (!ensureDirs()) {
        std::cerr << "[DeploymentManager] Failed to initialize deploy directories under "
                  << workDir_ << "/deploy\n";
    }
}

bool DeploymentManager::ensureDirs() {
    auto mkdirs = [](const std::string& path) -> bool {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) {
            std::cerr << "[DeploymentManager] Failed to create directory: " << path
                      << " (" << ec.message() << ")\n";
            return false;
        }
        return true;
    };

    if (!mkdirs(packagesDir_)) return false;
    if (!mkdirs(stagingDir_)) return false;
    if (!mkdirs(activeDir_)) return false;
    if (!mkdirs(backupDir_)) return false;
    if (!mkdirs(workDir_ + "/deploy/history")) return false;
    if (!mkdirs(metadataDir_)) return false;
    if (!mkdirs(stacksDir_)) return false;
    return true;
}

std::string DeploymentManager::nowTimestampForFilename() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ts.str();
}

bool DeploymentManager::copyFileWithPerms(const std::string& src, const std::string& dst) {
    std::error_code ec;
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    std::error_code pec;
    const auto perms = std::filesystem::status(src, pec).permissions();
    if (!pec) {
        std::filesystem::permissions(dst, perms, std::filesystem::perm_options::replace, pec);
    }
    return true;
}

bool DeploymentManager::validatePackageFile(const std::string& path, ValidationInfo& out) {
    out = ValidationInfo{};
    out.result = "failed";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        out.reason = "package missing";
        return false;
    }
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec) {
        out.reason = "unable to read file size";
        return false;
    }
    if (sz == 0) {
        out.reason = "package is empty";
        return false;
    }

    // Lightweight checksum: FNV-1a 64-bit over file bytes (no external deps).
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.reason = "unable to open file";
        return false;
    }
    std::uint64_t h = 1469598103934665603ull;
    char buf[8192];
    while (in.good()) {
        in.read(buf, sizeof(buf));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buf[i]);
            h *= 1099511628211ull;
        }
    }
    if (!in.eof() && in.fail()) {
        out.reason = "hash read failed";
        return false;
    }
    out.hash = hexU64(h);
    out.result = "success";
    out.reason = "basic checks passed";
    return true;
}

bool DeploymentManager::backupExistingActiveIfAny(const std::string& execBasename) {
    const std::string activePath = activeDir_ + "/" + execBasename;
    std::error_code ec;
    if (!std::filesystem::exists(activePath, ec)) {
        std::cout << "[DEPLOY] previous active backed up (none)\n";
        return true;
    }

    const std::string stamp = nowTimestampForFilename();
    const std::string backupPath = backupDir_ + "/" + execBasename + "." + stamp;

    std::filesystem::rename(activePath, backupPath, ec);
    if (!ec) {
        std::cout << "[DEPLOY] previous active backed up\n";
        return true;
    }

    // Cross-device or permission edge cases: fallback to copy+remove.
    if (!copyFileWithPerms(activePath, backupPath)) {
        std::cerr << "[DEPLOY] backup failed: " << ec.message() << "\n";
        return false;
    }
    std::filesystem::remove(activePath, ec);
    std::cout << "[DEPLOY] previous active backed up\n";
    return true;
}

bool DeploymentManager::writeDeployMetadataPlaceholder(const PackageInfo& info,
    const std::string& stagingPath,
    const std::string& activePath,
    const ValidationInfo& validation) const {
    // Lightweight placeholder for upcoming inventory/validation features.
    // One file per deploy action; format is intentionally simple for demo readability.
    const std::string stamp = nowTimestampForFilename();
    const std::string metaPath = metadataDir_ + "/deploy_" + info.execBasename + "_" + stamp + ".txt";

    std::ofstream out(metaPath, std::ios::trunc);
    if (!out) {
        std::cerr << "[DeploymentManager] Failed to write metadata: " << metaPath << "\n";
        return false;
    }
    out << "action=deploy\n";
    out << "feature=" << info.featureName << "\n";
    out << "version=" << info.version << "\n";
    out << "exec=" << info.execBasename << "\n";
    out << "mode=" << info.mode << "\n";
    out << "source=" << info.sourcePath << "\n";
    out << "staging=" << stagingPath << "\n";
    out << "active=" << activePath << "\n";
    out << "status=" << info.status << "\n";
    out << "deployedAt=" << info.deployedAt << "\n";
    out << "validation_result=" << validation.result << "\n";
    out << "validation_reason=" << validation.reason << "\n";
    if (!validation.hash.empty()) {
        out << "validation_hash_fnv1a64=" << validation.hash << "\n";
    }
    return true;
}

bool DeploymentManager::safeBasename(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return false;
    }
    return true;
}

bool DeploymentManager::inferPackage(const std::string& execBasename, PackageInfo& out) {
    out.execBasename = execBasename;
    out.mode = "primary";
    std::string core = execBasename;
    const std::string shadowSuf = "_Shadow";
    if (core.size() > shadowSuf.size() &&
        core.compare(core.size() - shadowSuf.size(), shadowSuf.size(), shadowSuf) == 0) {
        out.mode = "shadow";
        core.resize(core.size() - shadowSuf.size());
    }

    auto endsWith = [&](const std::string& suf) {
        return core.size() >= suf.size() &&
            core.compare(core.size() - suf.size(), suf.size(), suf) == 0;
    };

    if (core.rfind("AdasFeature_", 0) == 0) {
        out.featureName = "ADAS";
    } else if (core.rfind("NavigationFeature_", 0) == 0) {
        out.featureName = "NAV";
    } else if (core.rfind("EvEnergyFeature_", 0) == 0) {
        out.featureName = "ENERGY";
    } else {
        return false;
    }

    if (endsWith("_A")) {
        out.version = "A";
        return true;
    }
    if (endsWith("_B")) {
        out.version = "B";
        return true;
    }
    return false;
}

bool DeploymentManager::loadOrInitSelection(ConfigUpdater::Selection& s) {
    if (ConfigUpdater::loadSelection(selectionPath_, s)) {
        return true;
    }
    s = ConfigUpdater::Selection{};
    ConfigUpdater::saveSelection(selectionPath_, s);
    return true;
}

std::string DeploymentManager::stackFile(const std::string& domain) const {
    return stacksDir_ + "/" + domain + ".stack";
}

bool DeploymentManager::pushStack(const std::string& domain, const std::string& ab) {
    const std::string path = stackFile(domain);
    ConfigUpdater::Selection sel0{};
    loadOrInitSelection(sel0);
    std::string seed = "A";
    if (domain == "ADAS") {
        seed = sel0.adas;
    } else if (domain == "NAV") {
        seed = sel0.nav;
    } else {
        seed = sel0.energy;
    }

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
    }
    if (lines.empty()) {
        lines.push_back(seed);
    }
    if (!lines.empty() && lines.back() == ab) {
        return true;
    }
    lines.push_back(ab);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& l : lines) {
        out << l << '\n';
    }
    return true;
}

bool DeploymentManager::popStack(const std::string& domain, std::string& previousAb) {
    const std::string path = stackFile(domain);
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
    }
    if (lines.size() < 2) {
        return false;
    }
    lines.pop_back();
    previousAb = lines.back();

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& l : lines) {
        out << l << '\n';
    }
    return true;
}

void DeploymentManager::appendManifest(const PackageInfo& info) {
    std::ofstream out(manifestPath_, std::ios::app);
    if (!out) {
        return;
    }
    out << info.featureName << '|' << info.version << '|' << info.execBasename << '|'
        << info.deployedPath << '|' << info.deployedAt << '|' << info.status << '|'
        << info.mode << '\n';
}

std::string DeploymentManager::inventoryFile(const std::string& domain) const {
    return metadataDir_ + "/inventory_" + domain + ".state";
}

bool DeploymentManager::loadInventoryRecord(const std::string& domain, InventoryRecord& out) const {
    out = InventoryRecord{};
    std::ifstream in(inventoryFile(domain));
    if (!in) {
        return true;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "stable_version") {
            out.stableVersion = val;
        } else if (key == "candidate_version") {
            out.candidateVersion = val;
        } else if (key == "active_version") {
            out.activeVersion = val;
        } else if (key == "last_deployment_status") {
            out.lastDeploymentStatus = val;
        } else if (key == "last_deployed_at") {
            out.lastDeployedAt = val;
        } else if (key == "rollback_available") {
            out.rollbackAvailable = (val == "yes");
        }
    }
    return true;
}

bool DeploymentManager::saveInventoryRecord(const std::string& domain,
    const InventoryRecord& record) const {
    std::ofstream out(inventoryFile(domain), std::ios::trunc);
    if (!out) {
        std::cerr << "[DeploymentManager] Failed to write inventory: " << domain << "\n";
        return false;
    }
    out << "stable_version=" << record.stableVersion << "\n";
    out << "candidate_version=" << record.candidateVersion << "\n";
    out << "active_version=" << record.activeVersion << "\n";
    out << "last_deployment_status=" << record.lastDeploymentStatus << "\n";
    out << "last_deployed_at=" << record.lastDeployedAt << "\n";
    out << "rollback_available=" << (record.rollbackAvailable ? "yes" : "no") << "\n";
    return true;
}

bool DeploymentManager::hasRollbackHistory(const std::string& domain) const {
    // Prototype-level signal: selection stack depth only.
    // This does not guarantee binary backup integrity or runtime-safe rollback.
    std::ifstream in(stackFile(domain));
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (!trim(line).empty()) {
            ++count;
        }
    }
    return count >= 2;
}

bool DeploymentManager::updateInventoryOnDeploy(const PackageInfo& info,
    const std::string& deployStatus) {
    InventoryRecord record{};
    loadInventoryRecord(info.featureName, record);
    record.candidateVersion = info.version;
    record.lastDeploymentStatus = deployStatus;
    if (deployStatus != "FAILED") {
        record.lastDeployedAt = info.deployedAt;
    }
    record.rollbackAvailable = hasRollbackHistory(info.featureName);
    return saveInventoryRecord(info.featureName, record);
}

bool DeploymentManager::updateInventoryOnSelect(const std::string& domain, const std::string& ab) {
    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    record.activeVersion = ab;
    record.stableVersion = ab;
    record.lastDeployedAt = nowTimestampIso();
    record.rollbackAvailable = hasRollbackHistory(domain);
    return saveInventoryRecord(domain, record);
}

bool DeploymentManager::updateInventoryOnRollback(const std::string& domain, const std::string& ab) {
    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    record.activeVersion = ab;
    record.stableVersion = ab;
    record.lastDeploymentStatus = "ROLLED_BACK";
    record.lastDeployedAt = nowTimestampIso();
    record.rollbackAvailable = hasRollbackHistory(domain);
    return saveInventoryRecord(domain, record);
}

bool DeploymentManager::updateInventoryStatusOnly(const std::string& domain,
    const std::string& status) {
    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    record.lastDeploymentStatus = status;
    record.rollbackAvailable = hasRollbackHistory(domain);
    return saveInventoryRecord(domain, record);
}

std::string DeploymentManager::expectedPrimaryExec(const std::string& domain, const std::string& ab) {
    if (domain == "ADAS") {
        return "AdasFeature_" + ab;
    }
    if (domain == "NAV") {
        return "NavigationFeature_" + ab;
    }
    return "EvEnergyFeature_" + ab;
}

bool DeploymentManager::configContainsExpectedPrimary(const std::string& configPath,
    const std::string& domain,
    const std::string& ab) {
    const std::string expected = "feature " + expectedPrimaryExec(domain, ab);
    std::ifstream in(configPath);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line) == expected) {
            return true;
        }
    }
    return false;
}

bool DeploymentManager::runActivationHealthCheck(const std::string& domain,
    const std::string& ab,
    const std::string& platformConfigPath,
    HealthCheckInfo& out) const {
    out = HealthCheckInfo{};
    out.result = "failed";

    const std::string expectedExec = expectedPrimaryExec(domain, ab);
    const std::string activePath = activeDir_ + "/" + expectedExec;
    std::error_code ec;
    if (!std::filesystem::exists(activePath, ec)) {
        out.reason = "active binary missing";
        return false;
    }

    ConfigUpdater::Selection sel{};
    if (!ConfigUpdater::loadSelection(selectionPath_, sel)) {
        out.reason = "selection file missing";
        return false;
    }
    const std::string selectedAb =
        (domain == "ADAS") ? sel.adas : ((domain == "NAV") ? sel.nav : sel.energy);
    if (selectedAb != ab) {
        out.reason = "selection mismatch";
        return false;
    }

    if (!configContainsExpectedPrimary(platformConfigPath, domain, ab)) {
        out.reason = "config does not reference expected feature";
        return false;
    }

    InventoryRecord record{};
    loadInventoryRecord(domain, record);
    if (record.activeVersion != ab) {
        out.reason = "inventory active version mismatch";
        return false;
    }

    out.result = "success";
    out.reason = "basic activation checks passed";
    return true;
}

bool DeploymentManager::deployExecutable(const std::string& execBasename) {
    if (!ensureDirs()) {
        std::cerr << "[DeploymentManager] Cannot deploy: deploy directory init failed.\n";
        return false;
    }
    if (!safeBasename(execBasename)) {
        std::cerr << "[DeploymentManager] Invalid executable name.\n";
        return false;
    }
    PackageInfo info;
    if (!inferPackage(execBasename, info)) {
        std::cerr << "[DeploymentManager] Unrecognized feature binary: " << execBasename << "\n";
        return false;
    }

    const std::string src = packagesDir_ + "/" + execBasename;
    const std::string staging = stagingDir_ + "/" + execBasename;
    const std::string dst = activeDir_ + "/" + execBasename;
    info.sourcePath = src;
    info.deployedPath = dst;

    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        std::cerr << "[DeploymentManager] Missing package: " << src << "\n";
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    std::cout << "[DEPLOY] package found\n";

    if (!copyFileWithPerms(src, staging)) {
        std::cerr << "[DeploymentManager] Copy to staging failed.\n";
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    // Staging artifacts are intentionally retained for upcoming validation/approval steps.
    std::cout << "[DEPLOY] copied to staging\n";

    std::cout << "[DEPLOY] validation started\n";
    ValidationInfo validation{};
    if (!validatePackageFile(staging, validation)) {
        std::cerr << "[DEPLOY] validation failed: " << validation.reason << "\n";
        std::cerr << "[DEPLOY] deployment aborted before activation\n";

        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        if (!writeDeployMetadataPlaceholder(info, staging, dst, validation)) {
            std::cerr << "[DEPLOY] warning: metadata placeholder write failed\n";
        }
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    std::cout << "[DEPLOY] validation success\n";

    if (!backupExistingActiveIfAny(execBasename)) {
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }

    if (!copyFileWithPerms(staging, dst)) {
        std::cerr << "[DeploymentManager] Promote to active failed.\n";
        info.status = "failed";
        info.deployedAt = nowTimestampIso();
        updateInventoryOnDeploy(info, "FAILED");
        history_.append(info, "deploy");
        return false;
    }
    std::cout << "[DEPLOY] promoted to active\n";

    info.status = "success";
    info.deployedAt = nowTimestampIso();

    // Best-effort metadata for validation/inventory (prototype). Do not fail deploy on this.
    if (!writeDeployMetadataPlaceholder(info, staging, dst, validation)) {
        std::cerr << "[DEPLOY] warning: metadata placeholder write failed\n";
    }
    updateInventoryOnDeploy(info, "STAGED");

    history_.append(info, "deploy");
    appendManifest(info);
    std::cout << "[DeploymentManager] Deployed " << execBasename << " → " << dst << "\n";
    return true;
}

bool DeploymentManager::selectVersion(const std::string& domainIn, const std::string& ab,
    const std::string& platformConfigPath) {
    const std::string domain = toUpperDomain(domainIn);
    if (ab != "A" && ab != "B") {
        std::cerr << "[DeploymentManager] Version must be A or B.\n";
        return false;
    }
    if (domain != "ADAS" && domain != "NAV" && domain != "ENERGY") {
        std::cerr << "[DeploymentManager] Domain must be ADAS, NAV, or ENERGY.\n";
        return false;
    }

    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    if (domain == "ADAS") {
        sel.adas = ab;
    } else if (domain == "NAV") {
        sel.nav = ab;
    } else {
        sel.energy = ab;
    }

    if (!pushStack(domain, ab)) {
        return false;
    }
    if (!ConfigUpdater::saveSelection(selectionPath_, sel)) {
        std::cerr << "[DeploymentManager] Failed to write selection file.\n";
        return false;
    }
    if (!ConfigUpdater::applyToConfig(platformConfigPath, sel)) {
        std::cerr << "[DeploymentManager] Failed to update config: " << platformConfigPath << "\n";
        return false;
    }

    PackageInfo info;
    info.featureName = domain;
    info.version = ab;
    info.execBasename = {};
    info.sourcePath = selectionPath_;
    info.deployedPath = platformConfigPath;
    info.deployedAt = {};
    info.status = "active";
    info.mode = "selection";
    history_.append(info, "select");
    updateInventoryOnSelect(domain, ab);

    std::cout << "[RELEASE] activation requested\n";
    std::cout << "[RELEASE] config updated\n";
    std::cout << "[RELEASE] health check started\n";

    HealthCheckInfo health{};
    PackageInfo healthInfo;
    healthInfo.featureName = domain;
    healthInfo.version = ab;
    healthInfo.execBasename = expectedPrimaryExec(domain, ab);
    healthInfo.sourcePath = selectionPath_;
    healthInfo.deployedPath = platformConfigPath;
    healthInfo.deployedAt = nowTimestampIso();
    healthInfo.mode = "health_check";

    if (!runActivationHealthCheck(domain, ab, platformConfigPath, health)) {
        std::cerr << "[RELEASE] health check failed: " << health.reason << "\n";
        updateInventoryStatusOnly(domain, "HEALTH_CHECK_FAILED");
        healthInfo.status = "failed";
        history_.append(healthInfo, "health_check");

        std::cerr << "[RELEASE] automatic rollback started\n";
        // Reuse the existing stack-based rollback path: select() already pushed the new choice,
        // so rollbackDomain() restores the previous stable selection from the domain stack.
        if (rollbackDomain(domain, platformConfigPath)) {
            std::cout << "[RELEASE] rollback success\n";
        } else {
            std::cerr << "[RELEASE] rollback failed\n";
        }
        return false;
    }

    updateInventoryStatusOnly(domain, "ACTIVATED");
    std::cout << "[RELEASE] health check success\n";
    healthInfo.status = "success";
    history_.append(healthInfo, "health_check");
    std::cout << "[RELEASE] activation completed successfully\n";

    std::cout << "[DeploymentManager] Active " << domain << " = " << ab
              << " (config updated)\n";
    return true;
}

bool DeploymentManager::rollbackDomain(const std::string& domainIn,
    const std::string& platformConfigPath) {
    const std::string domain = toUpperDomain(domainIn);
    if (domain != "ADAS" && domain != "NAV" && domain != "ENERGY") {
        std::cerr << "[DeploymentManager] Domain must be ADAS, NAV, or ENERGY.\n";
        return false;
    }
    std::string prev;
    if (!popStack(domain, prev)) {
        std::cerr << "[DeploymentManager] No rollback history for " << domain << ".\n";
        return false;
    }

    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    if (domain == "ADAS") {
        sel.adas = prev;
    } else if (domain == "NAV") {
        sel.nav = prev;
    } else {
        sel.energy = prev;
    }
    if (!ConfigUpdater::saveSelection(selectionPath_, sel)) {
        return false;
    }
    if (!ConfigUpdater::applyToConfig(platformConfigPath, sel)) {
        return false;
    }

    PackageInfo info;
    info.featureName = domain;
    info.version = prev;
    info.status = "rollback";
    info.mode = "selection";
    history_.append(info, "rollback");
    updateInventoryOnRollback(domain, prev);
    std::cout << "[RELEASE] Rolled back " << domain << " to " << prev << "\n";
    return true;
}

bool DeploymentManager::syncConfig(const std::string& platformConfigPath) {
    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    return ConfigUpdater::applyToConfig(platformConfigPath, sel);
}

void DeploymentManager::printStatus(const std::string& platformConfigPath) const {
    std::cout << "=== STATUS PATHS ===\n";
    std::cout << "Working directory: " << workDir_ << "\n";
    std::cout << "Selection file:    " << selectionPath_ << "\n";
    std::cout << "Platform config:   " << platformConfigPath << "\n\n";
    printInventory();
}

void DeploymentManager::printInventory() const {
    ConfigUpdater::Selection sel{};
    ConfigUpdater::loadSelection(selectionPath_, sel);

    std::cout << "=== DEPLOYMENT / RELEASE STATUS ===\n";
    std::cout << "candidate=deployed candidate, active=selected version, stable=confirmed stable\n";
    for (const char* domain : kInventoryDomains) {
        InventoryRecord record{};
        loadInventoryRecord(domain, record);
        std::cout << std::left << std::setw(6) << domain
                  << " candidate=" << record.candidateVersion
                  << "   active=" << record.activeVersion
                  << "   stable=" << record.stableVersion
                  << "   status=" << record.lastDeploymentStatus
                  << "   rollback=" << (record.rollbackAvailable ? "yes" : "no");
        if (!record.lastDeployedAt.empty()) {
            std::cout << "   at=" << record.lastDeployedAt;
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    std::cout << "=== CURRENT SELECTION ===\n";
    std::cout << "ADAS=" << sel.adas << "   NAV=" << sel.nav << "   ENERGY=" << sel.energy << "\n\n";

    std::cout << "=== ACTIVE BINARIES ===\n";
    std::error_code ec;
    if (!std::filesystem::exists(activeDir_, ec)) {
        std::cout << "(empty)\n";
    } else {
        bool any = false;
        for (const auto& e : std::filesystem::directory_iterator(activeDir_, ec)) {
            if (e.is_regular_file()) {
                any = true;
                std::cout << e.path().filename().string() << "\n";
            }
        }
        if (!any) {
            std::cout << "(empty)\n";
        }
    }
    std::cout << "\n";

    std::cout << "=== RECENT HISTORY ===\n";
    const auto recentHistory = history_.readRecent(5);
    if (recentHistory.empty()) {
        std::cout << "(no history yet)\n";
    } else {
        for (const auto& line : recentHistory) {
            std::cout << line << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "=== RECENT MANIFEST ===\n";
    std::ifstream in(manifestPath_);
    if (!in) {
        std::cout << "(no manifest yet)\n";
        return;
    }
    std::string line;
    std::vector<std::string> tail;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            tail.push_back(line);
        }
    }
    constexpr std::size_t kMax = 5;
    if (tail.size() > kMax) {
        tail.erase(tail.begin(), tail.end() - static_cast<std::ptrdiff_t>(kMax));
    }
    for (const auto& l : tail) {
        std::cout << l << "\n";
    }
}

}  // namespace sdv::platform::deployment
