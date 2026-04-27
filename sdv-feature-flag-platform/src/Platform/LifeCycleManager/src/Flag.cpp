#include "LifeCycleManager/Flag.h"

namespace sdv::platform::lifecyclemgr {

Flag::Flag(const std::string& featureName)
    : featureName_(featureName)
    // Demo: main feature ON at start so run_sdv alone keeps Nav/ADAS/EV demo running.
    , enabled_(true)
    , killed_(false)
    , shadowMirrorEnabled_(false)
    , abVariant_() {}

void Flag::enable() {
    enabled_ = true;
    std::lock_guard<std::mutex> lock(subMutex_);
    subFeatures_.clear();
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

bool Flag::isSubFeatureEnabled(const std::string& subKey) const {
    if (subKey.empty()) {
        return true;
    }
    std::lock_guard<std::mutex> lock(subMutex_);
    const auto it = subFeatures_.find(subKey);
    if (it == subFeatures_.end()) {
        return true;
    }
    return it->second;
}

void Flag::setSubFeatureEnabled(const std::string& subKey, bool enabled) {
    if (subKey.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(subMutex_);
    if (enabled) {
        subFeatures_.erase(subKey);
    } else {
        subFeatures_[subKey] = false;
    }
}

}  // namespace sdv::platform::lifecyclemgr