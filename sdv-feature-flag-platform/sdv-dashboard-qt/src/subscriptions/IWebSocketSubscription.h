#pragma once

#include "IDataSubscription.h"

namespace sdv::dash {

/** Reserved for WebSocket fan-out from center services or gateways. */
class IWebSocketSubscription : public IDataSubscription {
    Q_OBJECT
public:
    explicit IWebSocketSubscription(QObject* parent = nullptr) : IDataSubscription(parent) {}
};

}  // namespace sdv::dash
