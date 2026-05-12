#pragma once

#include "IDataSubscription.h"

namespace sdv::dash {

/**
 * Host-local EventBus bridge (SHM/ZMQ/etc.) — not used on Windows dashboard builds.
 * Implementations should deserialize protobuf/JSON envelopes produced by EventBusHost.
 */
class IEventBusBridge : public IDataSubscription {
    Q_OBJECT
public:
    explicit IEventBusBridge(QObject* parent = nullptr) : IDataSubscription(parent) {}
};

}  // namespace sdv::dash
