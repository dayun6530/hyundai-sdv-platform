#pragma once

#include <atomic>
#include <optional>
#include <string>

namespace sdv::platform::lifecyclemgr {

/// Feature runtime flags: enable/disable, Shadow mirroring, Kill observation, A/B variant.
class Flag {
public:
    explicit Flag(const std::string& featureName);

    void enable();
    void disable();
    bool isEnabled() const;

    const std::string& getFeatureName() const;

    // ----- [Kill Switch] -----
    // Kill Switch (cooperative): set when matching KillSwitch event is consumed.
    void setKilled(bool killed);
    bool isKilled() const;
    
    // (develop 브랜치 호환용 Alias)
    void setKillSwitchLatched(bool latched) { setKilled(latched); }
    bool isKillSwitchLatched() const { return isKilled(); }

    // ----- [Shadow Mode] -----
    // Shadow Mode: primary-only mirror toggle.
    void setShadowMirrorEnabled(bool enabled);
    bool isShadowMirrorEnabled() const;

    // (develop 브랜치 호환용 Alias)
    void enableShadowMirroring() { setShadowMirrorEnabled(true); }
    void disableShadowMirroring() { setShadowMirrorEnabled(false); }
    bool isShadowMirroringEnabled() const { return isShadowMirrorEnabled(); }

    // ----- [A/B Test Mode] -----
    // A/B variant (CONVENTIONS 10.2).
    void setAbVariant(char variant);
    void clearAbVariant();
    std::optional<char> getAbVariant() const;

    // ----- [LifeCycle] -----
    /// Reset kill state for a new run() (enabled/disabled unchanged).
    void resetRunState();

private:
    std::string featureName_;

    std::atomic<bool> enabled_;
    std::atomic<bool> killed_;
    std::atomic<bool> shadowMirrorEnabled_;
    
    std::optional<char> abVariant_;
};

}  // namespace sdv::platform::lifecyclemgr