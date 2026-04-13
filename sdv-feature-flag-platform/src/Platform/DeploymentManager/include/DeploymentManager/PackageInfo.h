#pragma once

#include <string>

namespace sdv::platform::deployment {

/// Metadata for a deployable feature binary (file-based OTA slice).
struct PackageInfo {
    std::string featureName;   ///< Logical family key, e.g. "ADAS", "NAV", "ENERGY"
    std::string version;       ///< "A" or "B" (car line)
    std::string execBasename;  ///< e.g. "AdasFeature_B"
    std::string sourcePath;    ///< Path under deploy/packages at intake time
    std::string deployedPath;  ///< Path under deploy/active after copy
    std::string deployedAt;    ///< ISO-8601 UTC-ish local time string
    std::string status;        ///< e.g. "success", "failed"
    std::string mode;          ///< "primary" | "shadow" (shadow binary is separate exec)
};

}  // namespace sdv::platform::deployment
