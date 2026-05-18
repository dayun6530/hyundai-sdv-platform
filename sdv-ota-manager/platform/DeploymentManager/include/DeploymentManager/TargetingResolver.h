#pragma once

#include "DeploymentManager/VehicleRegistry.h"

#include <optional>
#include <string>
#include <vector>

namespace sdv::platform::deployment {

/// Parsed `target_selector` object from a campaign JSON file.
struct TargetSelector {
    std::string scope;
    std::vector<std::string> vehicle_ids;
    std::string group;
    std::string region;

    std::vector<std::string> compatible_models;
    std::vector<std::string> compatible_regions;
    std::vector<std::string> exclude_car_ids;
    std::optional<bool> require_online;
};

/// Optional filters from campaign root or manifest (merged with selector filters).
struct TargetingCompatibilityFilters {
    std::vector<std::string> compatible_models;
    std::vector<std::string> compatible_regions;
    std::vector<std::string> exclude_car_ids;
    std::optional<bool> require_online;
};

struct TargetingResult {
    bool ok{false};
    std::string scope;
    std::vector<std::string> resolved_car_ids;
    std::vector<std::string> warnings;
    std::vector<std::string> failures;
};

/// Resolves fleet targeting to concrete `car_id` values (read-only; no state writes).
class TargetingResolver {
public:
    /// Parses a JSON object string (contents of `target_selector`, including `{` `}`).
    static bool parseTargetSelector(const std::string& targetSelectorJson, TargetSelector& out, std::string& error);

    /// Resolves vehicles for \p selector using \p registry. Optional \p extra filters are merged
    /// (union of list fields; require_online true if either source requests it).
    static TargetingResult resolve(const TargetSelector& selector, const VehicleRegistry& registry,
        const TargetingCompatibilityFilters* extra = nullptr);

    /// Human-readable OK line for campaign-validate (scope summary).
    static std::string formatScopeOkLine(const TargetingResult& result, const TargetSelector& selector);

    /// Human-readable OK line: `resolved_targets count=N cars=...`
    static std::string formatResolvedTargetsLine(const TargetingResult& result, std::size_t maxCars = 10);
};

}  // namespace sdv::platform::deployment
