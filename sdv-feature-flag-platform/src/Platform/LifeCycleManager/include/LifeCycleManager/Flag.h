#pragma once

#include <string>
#include <atomic>
#include <optional>

namespace sdv::platform::lifecyclemgr {

/// Feature runtime flags: enable/disable, Shadow mirroring, Kill observation, A/B variant.
class Flag {
public:
    explicit Flag(const std::string& featureName);

    void enable();
    void disable();
    bool isEnabled() const;

    const std::string& getFeatureName() const;

    /// Shadow Mode (Primary): when true, `sendDomainEvent` also emits `ShadowMirrorData`.
    void enableShadowMirroring();
    void disableShadowMirroring();
    bool isShadowMirroringEnabled() const;

    /// Set when a KillSwitch control event targets this feature (observable; independent of `isEnabled`).
    void setKillSwitchLatched(bool latched);
    bool isKillSwitchLatched() const;

    /// A/B variant (CONVENTIONS 10.2): `variant` must be 'A' or 'B'.
    void setAbVariant(char variant);
    void clearAbVariant();
    std::optional<char> getAbVariant() const;

private:
    std::string featureName_;
    std::atomic<bool> enabled_;
    std::atomic<bool> shadowMirroringEnabled_;
    std::atomic<bool> killSwitchLatched_;
    /// 0 = unset; otherwise 'A' or 'B'.
    std::atomic<char> abVariant_;
};

}  // namespace sdv::platform::lifecyclemgr
