#pragma once

#include <optional>
#include <string>

namespace sdv::platform::deployment {

/// Metadata record for a deployable artifact handled by DeploymentManager.
///
/// This struct is used for **state/history/logging** and for describing file placement
/// (packages → staging → active). It intentionally does **not** model any runtime execution:
/// DeploymentManager prepares artifacts + state, while the platform runtime (LifeCycleManager)
/// decides what to execute based on `deploy/active/*` + config/state.
///
/// ## Docker-image based packaging (team direction)
/// The long-term deployment unit is a **feature-scoped Docker image**, e.g.
/// `sdv/adas-feature:v2.0`, `sdv/navigation-feature:v2.0`, `sdv/evenergy-feature:v2.0`.
///
/// To stay backward-compatible with the current executable-based flow, the Docker fields below
/// are **optional**. When unset, the record is treated as a legacy "executable artifact" entry.
struct PackageInfo {
    std::string featureName;   ///< Logical family key, e.g. "ADAS", "NAV", "ENERGY"
    std::string version;       ///< "A" or "B" (slot / line label in this demo)
    std::string execBasename;  ///< e.g. "AdasFeature" (artifact file basename)
    std::string sourcePath;    ///< Path under deploy/packages at intake time
    std::string deployedPath;  ///< Path under deploy/active after successful placement
    std::string deployedAt;    ///< ISO-8601 local time string (for logs/audit)
    std::string status;        ///< e.g. "success", "failed"
    std::string mode;          ///< "primary" | "shadow" (label for history/state; does not execute anything)

    // Optional Docker-image metadata (preferred deployment unit; may be empty in legacy flows).
    // NOTE: This step intentionally does not implement Docker runtime orchestration in DeploymentManager;
    // these fields exist to make "Docker image = deployment unit" explicit in metadata.
    std::optional<std::string> artifactType;   ///< "executable" | "docker_image" (empty => legacy executable)
    std::optional<std::string> dockerImage;    ///< e.g. "sdv/adas-feature:v2.0"
    std::optional<std::string> imageDigest;    ///< e.g. "sha256:..." (preferred: repo digest; fallback: local image Id)
    std::optional<std::string> containerRole;  ///< e.g. "primary" | "shadow" (not unified in this step)
};

}  // namespace sdv::platform::deployment
