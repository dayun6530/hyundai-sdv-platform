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

    void setKilled(bool killed);
    bool isKilled() const;

    void setShadowMirrorEnabled(bool enabled);
    bool isShadowMirrorEnabled() const;

    void enableShadowMirroring() { setShadowMirrorEnabled(true); }
    void disableShadowMirroring() { setShadowMirrorEnabled(false); }
    bool isShadowMirroringEnabled() const { return isShadowMirrorEnabled(); }

    void setKillSwitchLatched(bool latched) { setKilled(latched); }
    bool isKillSwitchLatched() const { return isKilled(); }

    void setAbVariant(char variant);
    void clearAbVariant();
    std::optional<char> getAbVariant() const;

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
