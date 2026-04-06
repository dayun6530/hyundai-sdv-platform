#include "Feature/NavigationFeature.h"

namespace sdv::feature::nav_ab {

void sendVariantB(NavigationFeature& f,
    sdv::platform::communications::eventbus::EventId id,
    const std::string& payloadBase,
    const char* ctxLabel,
    bool& any,
    bool pathEnabled) {
    if (!pathEnabled) {
        return;
    }
    any = true;
    if (!f.sendDomainEvent(id, payloadBase + " variant=B")) {
        f.log(sdv::platform::lifecyclemgr::LogLevel::warn,
            std::string(ctxLabel) + " 전송 실패 (B)");
    }
}

}  // namespace sdv::feature::nav_ab
