#pragma once

#include "LifeCycleManager/Feature.h"
#include "Feature/SubFeatures/SubFeatureTree.h"

#include <chrono>
#include <string>
#include <memory>

namespace sdv::feature {

class EvEnergyFeature : public virtual sdv::platform::lifecyclemgr::Feature {
public:
    explicit EvEnergyFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;
    std::string distributionVersionLabelForLog() const override;
    virtual void runEvEnergyMainLoop();

    int battery_;

    virtual void processVehicleLocation(const std::string& message);
    virtual void processRoadInfo(const std::string& message);

    void applyRoadBatteryModifiers(const std::string& message);
    bool checkBatteryEmptyAndQuit();

private:
    std::unique_ptr<sdv::feature::sub::CompositeNode> subTree_;
    std::chrono::steady_clock::time_point lastUphillDrainAt_{};
    std::chrono::steady_clock::time_point lastDownhillRegenAt_{};
    std::string lastRoadMessage_{};
    std::chrono::steady_clock::time_point lastRoadMessageAt_{};
};

}  // namespace sdv::feature
