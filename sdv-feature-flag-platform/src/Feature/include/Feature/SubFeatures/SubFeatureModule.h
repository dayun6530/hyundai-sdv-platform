#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sdv::feature::sub {

/**
 * Small, OO sub-feature module interface.
 *
 * Key idea:
 * - Platform-level Feature already owns the enable/disable state (Flag::isSubFeatureEnabled).
 * - Domain feature (Navigation/ADAS/EvEnergy) composes multiple sub-modules.
 * - When a sub-feature key is OFF, the parent simply does not dispatch to that module.
 *
 * This keeps external control plane contracts unchanged:
 *   FLAG_ON:<Feature>.<SUB> / FLAG_OFF:<Feature>.<SUB>
 */
class ISubFeatureModule {
public:
    virtual ~ISubFeatureModule() = default;
    virtual const char* key() const = 0;  // e.g. "LOCATION", "FCA", "RANGE"

    // Domain event hooks (modules implement what they need).
    virtual void onTick() {}
    virtual void onVehicleLocation(const std::string&) {}
    virtual void onRoadInfo(const std::string&) {}
};

struct CommonCallbacks {
    std::function<void(const std::string&)> logInfo;
    std::function<void(const std::string&)> logWarn;
    std::function<bool(int /*EventId as int*/, const std::string&)> sendDomainEvent;
};

inline std::vector<std::unique_ptr<ISubFeatureModule>> emptyModules() {
    return {};
}

}  // namespace sdv::feature::sub

