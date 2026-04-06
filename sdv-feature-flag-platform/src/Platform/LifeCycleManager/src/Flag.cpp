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

// ----- [Kill Switch] -----
void Flag::setKilled(bool killed) {
    killed_.store(killed);
}

bool Flag::isKilled() const {
    return killed_.load();
}

// ----- [Shadow Mode] -----
void Flag::setShadowMirrorEnabled(bool enabled) {
    shadowMirrorEnabled_.store(enabled);
}

bool Flag::isShadowMirrorEnabled() const {
    return shadowMirrorEnabled_.load();
}

// ----- [A/B Test Mode] -----
void Flag::setAbVariant(char variant) {
    // develop 브랜치의 안전한 방어 로직 채택
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

bool Flag::isAbModeA() const {
    return abVariant_.has_value() && *abVariant_ == 'A';
}

bool Flag::isAbModeB() const {
    return abVariant_.has_value() && *abVariant_ == 'B';
}

// ----- [LifeCycle] -----
void Flag::resetRunState() {
    killed_.store(false);
}

}  // namespace sdv::platform::lifecyclemgr