#include "DeploymentManager/DeploymentManager.h"

#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

}  // namespace

DeploymentManager::DeploymentManager(std::string workingDirectory)
    : history_(workingDirectory + "/deploy/history")
    , workDir_(std::move(workingDirectory))
    , packagesDir_(workDir_ + "/deploy/packages")
    , activeDir_(workDir_ + "/deploy/active")
    , selectionPath_(workDir_ + "/deploy/active_selection.conf")
    , stacksDir_(workDir_ + "/deploy/stacks")
    , manifestPath_(workDir_ + "/deploy/manifest.txt") {
    ensureDirs();
}

bool DeploymentManager::ensureDirs() {
    std::error_code ec;
    std::filesystem::create_directories(packagesDir_, ec);
    std::filesystem::create_directories(activeDir_, ec);
    std::filesystem::create_directories(workDir_ + "/deploy/history", ec);
    std::filesystem::create_directories(stacksDir_, ec);
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

bool DeploymentManager::deployExecutable(const std::string& execBasename) {
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
    const std::string dst = activeDir_ + "/" + execBasename;
    info.sourcePath = src;
    info.deployedPath = dst;

    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        std::cerr << "[DeploymentManager] Missing package: " << src << "\n";
        info.status = "failed";
        info.deployedAt = {};
        history_.append(info, "deploy");
        return false;
    }

    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "[DeploymentManager] Copy failed: " << ec.message() << "\n";
        info.status = "failed";
        history_.append(info, "deploy");
        return false;
    }
    {
        std::error_code pec;
        const auto perms = std::filesystem::status(src, pec).permissions();
        if (!pec) {
            std::filesystem::permissions(dst, perms, std::filesystem::perm_options::replace, pec);
        }
    }

    info.status = "success";
    {
        using clock = std::chrono::system_clock;
        const auto t = clock::to_time_t(clock::now());
        std::tm tm{};
        localtime_r(&t, &tm);
        std::ostringstream ts;
        ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        info.deployedAt = ts.str();
    }
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
    std::cout << "[DeploymentManager] Rolled back " << domain << " to " << prev << "\n";
    return true;
}

bool DeploymentManager::syncConfig(const std::string& platformConfigPath) {
    ConfigUpdater::Selection sel{};
    loadOrInitSelection(sel);
    return ConfigUpdater::applyToConfig(platformConfigPath, sel);
}

void DeploymentManager::printStatus(const std::string& platformConfigPath) const {
    ConfigUpdater::Selection sel{};
    ConfigUpdater::loadSelection(selectionPath_, sel);
    std::cout << "Working directory: " << workDir_ << "\n";
    std::cout << "Selection file:    " << selectionPath_ << "\n";
    std::cout << "Platform config:   " << platformConfigPath << "\n";
    std::cout << "Active ADAS:  " << sel.adas << "\n";
    std::cout << "Active NAV:   " << sel.nav << "\n";
    std::cout << "Active ENERGY:" << sel.energy << "\n";
}

void DeploymentManager::printInventory() const {
    std::cout << "--- deploy/active (binaries) ---\n";
    std::error_code ec;
    if (!std::filesystem::exists(activeDir_, ec)) {
        std::cout << "(empty)\n";
    } else {
        for (const auto& e : std::filesystem::directory_iterator(activeDir_, ec)) {
            if (e.is_regular_file()) {
                std::cout << e.path().filename().string() << "\n";
            }
        }
    }
    std::cout << "--- manifest (last deploys) ---\n";
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
    constexpr std::size_t kMax = 32;
    if (tail.size() > kMax) {
        tail.erase(tail.begin(), tail.end() - static_cast<std::ptrdiff_t>(kMax));
    }
    for (const auto& l : tail) {
        std::cout << l << "\n";
    }
}

}  // namespace sdv::platform::deployment
