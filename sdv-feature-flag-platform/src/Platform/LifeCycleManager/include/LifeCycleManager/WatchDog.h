#pragma once

#include "LifeCycleManager/Feature.h"

#include <map>
#include <chrono>

namespace sdv::platform::lifecyclemgr {

class WatchDog : public Feature {
public:
    WatchDog(EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    void analyzeEvent(const Event& event);
    void checkHealth();
    void logFlagEvent(const Event& event);

    std::map<int, int> eventCounts_;
    std::chrono::steady_clock::time_point lastEventTime_;
    int checkIntervalSeconds_;
    int timeoutSeconds_;
};

}  // namespace sdv::platform::lifecyclemgr
