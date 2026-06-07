#pragma once

#include "twin/TwinTypes.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace sdv::dash::carla::integration {

struct CarlaGnssSample {
    double latitudeDeg{0.0};
    double longitudeDeg{0.0};
    double altitudeM{0.0};
};

struct CarlaImuSample {
    double accelXMps2{0.0};
    double accelYMps2{0.0};
    double accelZMps2{0.0};
    double gyroXRps{0.0};
    double gyroYRps{0.0};
    double gyroZRps{0.0};
};

struct CarlaVehicleSnapshot {
    qint64 stampNs{0};
    QString frameId{QStringLiteral("map")};
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yawRad{0.0};
    double speedMps{0.0};
    double longitudinalVelocityMps{0.0};
    double lateralVelocityMps{0.0};
    double headingRateRps{0.0};
    double steeringAngleDeg{0.0};
    double throttle{0.0};
    double brake{0.0};
    int gear{3};
    bool adasActive{true};
};

struct CarlaActorSnapshot {
    QString objectId;
    QString classification;
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yawRad{0.0};
    double lengthM{4.5};
    double widthM{2.0};
    double heightM{1.5};
    double velocityXMps{0.0};
    double velocityYMps{0.0};
    double existenceProbability{0.9};
};

struct CarlaRoutePoint {
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yawRad{0.0};
};

struct CarlaWorldSnapshot {
    CarlaVehicleSnapshot vehicle;
    CarlaGnssSample gnss;
    CarlaImuSample imu;
    QVector<CarlaActorSnapshot> actors;
    QVector<CarlaRoutePoint> routePoints;
    sdv::dash::twin::TwinWorldState twin;
    QImage camera;
    bool hasCamera{false};
};

}  // namespace sdv::dash::carla::integration
