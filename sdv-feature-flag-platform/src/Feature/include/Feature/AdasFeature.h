#pragma once

#include "LifeCycleManager/Feature.h"

#include <string>

namespace sdv::feature {

class AdasFeature;

// 질문자님이 작성하신 A/B 모드 분기 함수들
namespace adas_ab {
void onLocationA(AdasFeature& f, const std::string& message);
void onLocationB(AdasFeature& f, const std::string& message);
void onRoadA(AdasFeature& f, const std::string& message);
void onRoadB(AdasFeature& f, const std::string& message);
}  // namespace adas_ab

// Domain output uses Feature::sendDomainEvent — Shadow mirroring is handled entirely in the base class.
class AdasFeature : public sdv::platform::lifecyclemgr::Feature {
public:
    explicit AdasFeature(sdv::platform::communications::eventbus::EventBusInterface& eventBus);

protected:
    void execute() override;

private:
    void processVehicleLocation(const std::string& message);
    void processRoadInfo(const std::string& message);
};

}  // namespace sdv::feature