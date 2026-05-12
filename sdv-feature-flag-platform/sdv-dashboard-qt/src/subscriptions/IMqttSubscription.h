#pragma once

#include "IDataSubscription.h"

namespace sdv::dash {

/** MQTT subscriber facade — concrete wiring lives next to DashboardDataHub (Qt MQTT optional). */
class IMqttSubscription : public IDataSubscription {
    Q_OBJECT
public:
    explicit IMqttSubscription(QObject* parent = nullptr) : IDataSubscription(parent) {}
};

}  // namespace sdv::dash
