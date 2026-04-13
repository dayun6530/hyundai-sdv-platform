#pragma once

#include "DeploymentManager/PackageInfo.h"

#include <string>
#include <vector>

namespace sdv::platform::deployment {

/// Append-only deployment / rollback audit log (plain text, one record per line).
class DeploymentHistory {
public:
    explicit DeploymentHistory(std::string historyDir);

    /// Writes a pipe-delimited record and optional JSON-ish detail line for humans.
    void append(const PackageInfo& info, const std::string& action);

    /// Returns recent records (newest last), capped for CLI display.
    std::vector<std::string> readRecent(std::size_t maxLines) const;

    const std::string& path() const { return logFilePath_; }

private:
    std::string logFilePath_;
};

}  // namespace sdv::platform::deployment
