#pragma once

#include <string>
#include <atomic>

namespace sdv::platform::lifecyclemgr {

class Flag {
public:
    explicit Flag(const std::string& featureName);

    void enable();
    void disable();
    bool isEnabled() const;

    const std::string& getFeatureName() const;

private:
    std::string featureName_;
    std::atomic<bool> enabled_;
};

}  // namespace sdv::platform::lifecyclemgr
