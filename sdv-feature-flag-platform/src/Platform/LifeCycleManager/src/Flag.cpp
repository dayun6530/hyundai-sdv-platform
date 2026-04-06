#include "LifeCycleManager/Flag.h"

namespace sdv::platform::lifecyclemgr {

Flag::Flag(const std::string& featureName)
    : featureName_(featureName)
    , enabled_(true)
    , shadowMirroringEnabled_(false)
    , killSwitchLatched_(false)
    , abVariant_(0) {}

void Flag::enable() {
    enabled_ = true;
}

void Flag::disable() {
    enabled_ = false;
}

bool Flag::isEnabled() const {
    return enabled_;
}

const std::string& Flag::getFeatureName() const {
    return featureName_;
}

void Flag::enableShadowMirroring() {
    shadowMirroringEnabled_ = true;
}

void Flag::disableShadowMirroring() {
    shadowMirroringEnabled_ = false;
}

bool Flag::isShadowMirroringEnabled() const {
    return shadowMirroringEnabled_;
}

void Flag::setKillSwitchLatched(bool latched) {
    killSwitchLatched_ = latched;
}

bool Flag::isKillSwitchLatched() const {
    return killSwitchLatched_;
}

void Flag::setAbVariant(char variant) {
    if (variant == 'A' || variant == 'B') {
        abVariant_ = variant;
    }
}

void Flag::clearAbVariant() {
    abVariant_ = 0;
}

std::optional<char> Flag::getAbVariant() const {
    const char v = abVariant_.load();
    if (v == 0) {
        return std::nullopt;
    }
    return v;
}

}  // namespace sdv::platform::lifecyclemgr
