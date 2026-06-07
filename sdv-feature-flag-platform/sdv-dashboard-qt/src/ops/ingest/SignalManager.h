#pragma once

#include <QObject>

#include "ops/domain/OpsTypes.h"

namespace sdv::dash::ops {

/// Parses sdv_ros_bridge telemetry envelopes on the GUI thread and emits domain signals.
class SignalManager : public QObject {
    Q_OBJECT
public:
    explicit SignalManager(QObject* parent = nullptr);

public slots:
    void ingestJsonFrame(const QByteArray& utf8Frame);

signals:
    void bridgeHelloReceived();
    void parseError(const QString& reason);

    void vehicleStateUpdated(const sdv::dash::ops::VehicleState& state);
    void egoPoseUpdated(const sdv::dash::ops::EgoPose& pose);
    void trajectoryUpdated(const sdv::dash::ops::Trajectory& trajectory);
    void perceptionUpdated(const sdv::dash::ops::PerceptionObjectList& objects);

private:
    quint64 framesReceived_{0};
    quint64 framesDropped_{0};
};

}  // namespace sdv::dash::ops
