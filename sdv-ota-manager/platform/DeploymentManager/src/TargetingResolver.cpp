#include "DeploymentManager/TargetingResolver.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

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

std::vector<std::string> jsonStringArrayField(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    try {
        const std::regex blockRe("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
        std::smatch block;
        if (!std::regex_search(json, block, blockRe) || block.size() < 2) {
            return out;
        }
        const std::regex itemRe("\"([^\"]*)\"");
        auto begin = std::sregex_iterator(block[1].first, block[1].second, itemRe);
        const auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            if (it->size() >= 2) {
                out.push_back((*it)[1].str());
            }
        }
    } catch (...) {
    }
    return out;
}

void mergeStringLists(std::vector<std::string>& dest, const std::vector<std::string>& src) {
    dest.insert(dest.end(), src.begin(), src.end());
}

void dedupePreserveOrder(std::vector<std::string>& ids) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> unique;
    unique.reserve(ids.size());
    for (const auto& id : ids) {
        if (seen.insert(id).second) {
            unique.push_back(id);
        }
    }
    ids = std::move(unique);
}

bool containsId(const std::vector<std::string>& list, const std::string& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

bool listContainsValue(const std::vector<std::string>& list, const std::string& value) {
    for (const auto& item : list) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

std::vector<VehicleRegistry::Vehicle> resolveScopeCandidates(const TargetSelector& selector,
    const VehicleRegistry& registry,
    TargetingResult& result) {
    std::vector<VehicleRegistry::Vehicle> candidates;

    if (selector.scope == "vehicle") {
        std::vector<std::string> vehicle_ids = selector.vehicle_ids;
        dedupePreserveOrder(vehicle_ids);
        if (vehicle_ids.empty()) {
            result.failures.push_back("target_selector vehicle scope requires non-empty vehicle_ids");
            return candidates;
        }
        for (const auto& car_id : vehicle_ids) {
            const auto vehicle = registry.getByCarId(car_id);
            if (!vehicle) {
                result.failures.push_back("vehicle not in registry: " + car_id);
            } else {
                candidates.push_back(*vehicle);
            }
        }
        return candidates;
    }

    if (selector.scope == "group") {
        if (selector.group.empty()) {
            result.failures.push_back("target_selector group scope requires group");
            return candidates;
        }
        const auto vehicles = registry.findByGroup(selector.group);
        if (vehicles.empty()) {
            result.failures.push_back("target_selector scope=group group=" + selector.group
                                     + " resolved no vehicles");
            return candidates;
        }
        return vehicles;
    }

    if (selector.scope == "region") {
        if (selector.region.empty()) {
            result.failures.push_back("target_selector region scope requires region");
            return candidates;
        }
        const auto vehicles = registry.findByRegion(selector.region);
        if (vehicles.empty()) {
            result.failures.push_back("target_selector scope=region region=" + selector.region
                                     + " resolved no vehicles");
            return candidates;
        }
        return vehicles;
    }

    if (selector.scope == "group_region") {
        if (selector.group.empty() || selector.region.empty()) {
            result.failures.push_back("target_selector group_region scope requires group and region");
            return candidates;
        }
        const auto vehicles = registry.findByGroupAndRegion(selector.group, selector.region);
        if (vehicles.empty()) {
            result.failures.push_back("target_selector scope=group_region group=" + selector.group + " region="
                                     + selector.region + " resolved no vehicles");
            return candidates;
        }
        return vehicles;
    }

    if (selector.scope.empty()) {
        result.failures.push_back("target_selector missing scope");
    } else {
        result.failures.push_back("unsupported target_selector scope: " + selector.scope);
    }
    return candidates;
}

struct MergedFilters {
    std::vector<std::string> compatible_models;
    std::vector<std::string> compatible_regions;
    std::vector<std::string> exclude_car_ids;
    bool require_online{false};
};

MergedFilters mergeFilters(const TargetSelector& selector, const TargetingCompatibilityFilters* extra) {
    MergedFilters merged;
    merged.compatible_models = selector.compatible_models;
    merged.compatible_regions = selector.compatible_regions;
    merged.exclude_car_ids = selector.exclude_car_ids;
    merged.require_online = selector.require_online.value_or(false);
    if (extra != nullptr) {
        mergeStringLists(merged.compatible_models, extra->compatible_models);
        mergeStringLists(merged.compatible_regions, extra->compatible_regions);
        mergeStringLists(merged.exclude_car_ids, extra->exclude_car_ids);
        if (extra->require_online.value_or(false)) {
            merged.require_online = true;
        }
    }
    dedupePreserveOrder(merged.compatible_models);
    dedupePreserveOrder(merged.compatible_regions);
    dedupePreserveOrder(merged.exclude_car_ids);
    return merged;
}

void applyCompatibilityFilters(const std::vector<VehicleRegistry::Vehicle>& candidates,
    const MergedFilters& filters,
    TargetingResult& result) {
    std::unordered_set<std::string> warned;
    auto warnOnce = [&](const std::string& msg) {
        if (warned.insert(msg).second) {
            result.warnings.push_back(msg);
        }
    };

    for (const VehicleRegistry::Vehicle& vehicle : candidates) {
        const std::string& car_id = vehicle.car_id;

        if (containsId(filters.exclude_car_ids, car_id)) {
            continue;
        }

        if (!filters.compatible_models.empty()) {
            if (vehicle.model.empty()) {
                warnOnce("compatible_models filter set but vehicle " + car_id + " has no model in registry");
            } else if (!listContainsValue(filters.compatible_models, vehicle.model)) {
                continue;
            }
        }

        if (!filters.compatible_regions.empty()) {
            if (vehicle.region.empty()) {
                warnOnce("compatible_regions filter set but vehicle " + car_id + " has no region in registry");
            } else if (!listContainsValue(filters.compatible_regions, vehicle.region)) {
                continue;
            }
        }

        if (filters.require_online) {
            if (vehicle.status.empty()) {
                warnOnce("require_online set but vehicle " + car_id + " has no status in registry; filter skipped");
            } else if (toLower(vehicle.status) != "online") {
                result.warnings.push_back("vehicle " + car_id + " excluded: status is not online (status="
                                         + vehicle.status + ")");
                continue;
            }
        }

        result.resolved_car_ids.push_back(car_id);
    }

    dedupePreserveOrder(result.resolved_car_ids);
    std::sort(result.resolved_car_ids.begin(), result.resolved_car_ids.end());

    if (!filters.exclude_car_ids.empty() && result.resolved_car_ids.empty() && !candidates.empty()
        && result.failures.empty()) {
        result.failures.push_back("compatibility filters excluded all resolved vehicles");
    }
}

}  // namespace

bool TargetingResolver::parseTargetSelector(const std::string& targetSelectorJson, TargetSelector& out,
    std::string& error) {
    if (targetSelectorJson.empty()) {
        error = "missing target_selector object";
        return false;
    }
    out = TargetSelector{};
    out.scope = jsonStringField(targetSelectorJson, "scope").value_or("");
    out.vehicle_ids = jsonStringArrayField(targetSelectorJson, "vehicle_ids");
    out.group = jsonStringField(targetSelectorJson, "group").value_or("");
    out.region = jsonStringField(targetSelectorJson, "region").value_or("");
    out.compatible_models = jsonStringArrayField(targetSelectorJson, "compatible_models");
    out.compatible_regions = jsonStringArrayField(targetSelectorJson, "compatible_regions");
    out.exclude_car_ids = jsonStringArrayField(targetSelectorJson, "exclude_car_ids");
    out.require_online = jsonBoolField(targetSelectorJson, "require_online");
    return true;
}

TargetingResult TargetingResolver::resolve(const TargetSelector& selector, const VehicleRegistry& registry,
    const TargetingCompatibilityFilters* extra) {
    TargetingResult result;
    result.scope = selector.scope;

    const auto candidates = resolveScopeCandidates(selector, registry, result);
    if (!result.failures.empty()) {
        result.ok = false;
        return result;
    }

    const MergedFilters filters = mergeFilters(selector, extra);
    applyCompatibilityFilters(candidates, filters, result);

    if (result.resolved_car_ids.empty()) {
        if (result.failures.empty()) {
            if (selector.scope == "group" && !selector.group.empty()) {
                result.failures.push_back("target_selector scope=group group=" + selector.group
                                          + " resolved no vehicles");
            } else if (selector.scope == "region" && !selector.region.empty()) {
                result.failures.push_back("target_selector scope=region region=" + selector.region
                                          + " resolved no vehicles");
            } else if (selector.scope == "group_region") {
                result.failures.push_back("target_selector scope=group_region group=" + selector.group + " region="
                                          + selector.region + " resolved no vehicles");
            } else if (selector.scope == "vehicle") {
                result.failures.push_back("target_selector vehicle scope resolved no vehicles");
            } else {
                result.failures.push_back("target_selector resolved no vehicles");
            }
        }
        result.ok = false;
        return result;
    }

    if (!result.failures.empty()) {
        result.ok = false;
        return result;
    }

    result.ok = true;
    return result;
}

std::string TargetingResolver::formatScopeOkLine(const TargetingResult& result, const TargetSelector& selector) {
    std::ostringstream cars;
    for (std::size_t i = 0; i < result.resolved_car_ids.size(); ++i) {
        if (i) {
            cars << ",";
        }
        cars << result.resolved_car_ids[i];
    }
    const std::string joined = cars.str();

    if (result.scope == "vehicle") {
        return "target_selector scope=vehicle resolved_targets=" + joined;
    }
    if (result.scope == "group") {
        return "target_selector scope=group group=" + selector.group + " resolved_targets=" + joined;
    }
    if (result.scope == "region") {
        return "target_selector scope=region region=" + selector.region + " resolved_targets=" + joined;
    }
    if (result.scope == "group_region") {
        return "target_selector scope=group_region group=" + selector.group + " region=" + selector.region
               + " resolved_targets=" + joined;
    }
    return "target_selector scope=" + result.scope + " resolved_targets=" + joined;
}

std::string TargetingResolver::formatResolvedTargetsLine(const TargetingResult& result, std::size_t maxCars) {
    std::ostringstream line;
    line << "resolved_targets count=" << result.resolved_car_ids.size() << " cars=";
    const std::size_t show = std::min(maxCars, result.resolved_car_ids.size());
    for (std::size_t i = 0; i < show; ++i) {
        if (i) {
            line << ",";
        }
        line << result.resolved_car_ids[i];
    }
    if (result.resolved_car_ids.size() > maxCars) {
        line << ",...";
    }
    return line.str();
}

}  // namespace sdv::platform::deployment
