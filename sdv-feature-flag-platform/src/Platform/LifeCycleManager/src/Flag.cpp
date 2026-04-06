#include "LifeCycleManager/Flag.h"

namespace sdv::platform::lifecyclemgr {

Flag::Flag(const std::string& featureName)
    : featureName_(featureName)
    , enabled_(true)
    , killed_(false)
    , shadowMirrorEnabled_(false)
    , abVariant_() {}

// ----- [Enable / Disable] -----

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
    // 안전한 입력만 허용
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

// ----- [LifeCycle] -----

void Flag::resetRunState() {
    // run() 새로 시작할 때 kill 상태만 초기화
    killed_.store(false);
}

}  // namespace sdv::platform::lifecyclemgr