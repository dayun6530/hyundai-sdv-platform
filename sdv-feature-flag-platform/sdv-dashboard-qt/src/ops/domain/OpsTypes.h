#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace sdv::dash::ops {

struct VehicleState {
    qint64 stampNs{0};
    QString frameId;
    double longitudinalVelocityMps{0.0};
    double lateralVelocityMps{0.0};
    double headingRateRps{0.0};
    double speedMps{0.0};

    static VehicleState fromJson(const QJsonObject& data, qint64 envelopeStampNs, const QString& envelopeFrameId);
    bool isValid() const { return stampNs > 0 || !frameId.isEmpty(); }
};

struct EgoPose {
    qint64 stampNs{0};
    QString frameId;
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double qw{1.0};
    double qx{0.0};
    double qy{0.0};
    double qz{0.0};
    double yawRad{0.0};
    double speedMps{0.0};
    QString stampIso;

    static EgoPose fromJson(const QJsonObject& data, qint64 envelopeStampNs, const QString& envelopeFrameId);
    bool isValid() const { return stampNs > 0; }
};

struct TrajectoryWaypoint {
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yawRad{0.0};
    double longitudinalVelocityMps{0.0};
    double timeFromStartSec{0.0};
};

struct Trajectory {
    qint64 stampNs{0};
    QString frameId;
    QVector<TrajectoryWaypoint> points;

    static Trajectory fromJson(const QJsonObject& data, qint64 envelopeStampNs, const QString& envelopeFrameId);
    bool isValid() const { return !points.isEmpty(); }
};

struct PerceptionObject {
    QString objectId;
    QString classification;
    double existenceProbability{0.0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yawRad{0.0};
    double lengthM{0.0};
    double widthM{0.0};
    double heightM{0.0};
    double velocityXMps{0.0};
    double velocityYMps{0.0};
    /** traffic_light only: red | yellow | green | unknown */
    QString trafficLightState;
};

struct PerceptionObjectList {
    qint64 stampNs{0};
    QString frameId;
    QVector<PerceptionObject> objects;

    static PerceptionObjectList fromJson(const QJsonObject& data, qint64 envelopeStampNs, const QString& envelopeFrameId);
    bool isValid() const { return stampNs > 0; }
};

struct ParsedTelemetryEnvelope {
    int schemaVersion{0};
    QString type;
    qint64 stampNs{0};
    QString frameId;
    QJsonObject data;
    bool isValid() const { return !type.isEmpty() && !data.isEmpty(); }
};

}  // namespace sdv::dash::ops

Q_DECLARE_METATYPE(sdv::dash::ops::VehicleState)
Q_DECLARE_METATYPE(sdv::dash::ops::EgoPose)
Q_DECLARE_METATYPE(sdv::dash::ops::Trajectory)
Q_DECLARE_METATYPE(sdv::dash::ops::PerceptionObjectList)
