#include "DeploymentManager/DeploymentHistory.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace sdv::platform::deployment {

DeploymentHistory::DeploymentHistory(std::string historyDir)
    : logFilePath_(std::move(historyDir) + "/deployment.log") {}

void DeploymentHistory::append(const PackageInfo& info, const std::string& action) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(logFilePath_).parent_path(), ec);

    std::ofstream out(logFilePath_, std::ios::app);
    if (!out) {
        return;
    }
    // action|feature|version|exec|status|mode|deployedAt|source|dest
    out << action << '|' << info.featureName << '|' << info.version << '|'
        << info.execBasename << '|' << info.status << '|' << info.mode << '|'
        << info.deployedAt << '|' << info.sourcePath << '|' << info.deployedPath
        << '\n';
}

std::vector<std::string> DeploymentHistory::readRecent(std::size_t maxLines) const {
    std::vector<std::string> all;
    std::ifstream in(logFilePath_);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            all.push_back(std::move(line));
        }
    }
    if (all.size() <= maxLines) {
        return all;
    }
    return std::vector<std::string>(all.end() - static_cast<std::ptrdiff_t>(maxLines),
        all.end());
}

}  // namespace sdv::platform::deployment
