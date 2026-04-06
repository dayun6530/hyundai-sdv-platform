#include "LifeCycleManager/Flag.h"

namespace sdv::platform::lifecyclemgr {

Flag::Flag(const std::string& featureName)
    : featureName_(featureName)
    , enabled_(true)
    , killed_(false)
    , shadowMirrorEnabled_(false)
    , abVariant_() {}

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

void Flag::setKilled(bool killed) {
    killed_.store(killed);
}

bool Flag::isKilled() const {
    return killed_.load();
}

void Flag::setShadowMirrorEnabled(bool enabled) {
    shadowMirrorEnabled_.store(enabled);
}

bool Flag::isShadowMirrorEnabled() const {
    return shadowMirrorEnabled_.load();
}

void Flag::setAbVariant(char variant) {
    if (variant == 'A' || variant == 'B') {
        abVariant_ = variant;
    }
}

void Flag::clearAbVariant() {
    abVariant_.reset();
}

std::optional<char> Flag::getAbVariant() const {
    return abVariant_;
}

void Flag::resetRunState() {
    killed_.store(false);
}

}  // namespace sdv::platform::lifecyclemgr
