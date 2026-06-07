#pragma once

#include <QByteArray>
#include <QImage>
#include <QVector>

#include "carla/integration/CarlaTypes.h"
#include "ops/domain/OpsTypes.h"

namespace sdv::dash::carla::integration {

/** Builds Phase-1 telemetry JSON envelopes (schema_version 1). */
class CarlaTelemetrySerializer {
public:
    static QByteArray bridgeHello();
    static QByteArray vehicleState(const CarlaVehicleSnapshot& vehicle);
    static QByteArray egoPose(const CarlaVehicleSnapshot& vehicle);
    static QByteArray trajectory(const sdv::dash::ops::Trajectory& trajectory);
    static QByteArray perceptionObjects(const sdv::dash::ops::PerceptionObjectList& objects);
    static QByteArray frontCamera(const QImage& image, qint64 stampNs, const QString& frameId);

    static QVector<QByteArray> snapshotToTelemetryFrames(
        const CarlaWorldSnapshot& snapshot,
        const sdv::dash::ops::Trajectory& laneTrajectory,
        const sdv::dash::ops::PerceptionObjectList& perception);
};

}  // namespace sdv::dash::carla::integration
