#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class AdasFeature;

namespace adas_ab {
void onLocationA(AdasFeature& f, const std::string& message);
void onLocationB(AdasFeature& f, const std::string& message);
void onRoadA(AdasFeature& f, const std::string& message);
void onRoadB(AdasFeature& f, const std::string& message);
}  // namespace adas_ab

class AdasFeature : public sdv::platform::lifecyclemgr::Feature {
    friend void adas_ab::onLocationA(AdasFeature&, const std::string&);
    friend void adas_ab::onLocationB(AdasFeature&, const std::string&);
    friend void adas_ab::onRoadA(AdasFeature&, const std::string&);
    friend void adas_ab::onRoadB(AdasFeature&, const std::string&);

public:
    explicit AdasFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    void processVehicleLocation(const std::string& message);
    void processRoadInfo(const std::string& message);
};

}  // namespace sdv::feature
