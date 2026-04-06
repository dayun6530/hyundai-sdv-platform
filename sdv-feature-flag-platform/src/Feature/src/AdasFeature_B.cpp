#include "Feature/AdasFeature.h"
#include "FeatureCommunication/EventBus/EventId.h"

namespace sdv::feature::adas_ab {

void onLocationB(AdasFeature& f, const std::string& message) {
    (void)message;
    f.log(sdv::platform::lifecyclemgr::LogLevel::info,
        "ADAS B: 주변 차량/장애물 감지 영역 업데이트");
    f.sendDomainEvent(sdv::platform::communications::eventbus::EventId::AdasDecisionUpdate,
        "ADAS_DECISION source=location status=tracking variant=B");
}

void onRoadB(AdasFeature& f, const std::string& message) {
    (void)message;
    f.log(sdv::platform::lifecyclemgr::LogLevel::info,
        "ADAS B: 경로·속도 계획 반영 (자율주행)");
    f.sendDomainEvent(sdv::platform::communications::eventbus::EventId::AdasDecisionUpdate,
        "ADAS_DECISION source=road status=planning variant=B");
}

}  // namespace sdv::feature::adas_ab
