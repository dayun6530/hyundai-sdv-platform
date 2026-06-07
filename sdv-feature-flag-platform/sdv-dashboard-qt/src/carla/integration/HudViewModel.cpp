#include "carla/integration/HudViewModel.h"

#include <QtMath>

namespace sdv::dash::carla::integration {

HudViewModel::HudViewModel(QObject* parent) : QObject(parent) {}

void HudViewModel::applySnapshot(const CarlaWorldSnapshot& snapshot)
{
    const auto& v = snapshot.vehicle;
    speedKmh_ = v.speedMps * 3.6;
    gear_ = v.gear;
    headingDeg_ = qRadiansToDegrees(v.yawRad);
    accelerationMps2_ = qSqrt(snapshot.imu.accelXMps2 * snapshot.imu.accelXMps2 +
                              snapshot.imu.accelYMps2 * snapshot.imu.accelYMps2);
    adasActive_ = v.adasActive;

    if (snapshot.gnss.latitudeDeg != 0.0 || snapshot.gnss.longitudeDeg != 0.0) {
        gpsLine_ = QStringLiteral("GNSS %1 , %2 · %3 m")
                       .arg(snapshot.gnss.latitudeDeg, 0, 'f', 5)
                       .arg(snapshot.gnss.longitudeDeg, 0, 'f', 5)
                       .arg(snapshot.gnss.altitudeM, 0, 'f', 1);
    } else {
        gpsLine_ = QStringLiteral("MAP %1 , %2 m").arg(v.x, 0, 'f', 1).arg(v.y, 0, 'f', 1);
    }

    emit changed();
}

}  // namespace sdv::dash::carla::integration
