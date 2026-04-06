#pragma once

#include <atomic>
#include <optional>
#include <string>

namespace sdv::platform::lifecyclemgr {

class Flag {
public:
    explicit Flag(const std::string& featureName);

    void enable();
    void disable();
    bool isEnabled() const;

    const std::string& getFeatureName() const;

    // Kill Switch (cooperative): set when matching KillSwitch event is consumed.
    void setKilled(bool killed);
    bool isKilled() const;

    // Shadow Mode: primary-only mirror toggle (see Feature::pollEvents).
    void setShadowMirrorEnabled(bool enabled);
    bool isShadowMirrorEnabled() const;

    // A/B variant (optional; CONVENTIONS 10.2).
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
