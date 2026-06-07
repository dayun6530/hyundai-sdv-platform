#pragma once

#include <QObject>
#include <QString>

#include "carla/integration/CarlaTypes.h"

namespace sdv::dash::carla::integration {

/** Presentation model for cockpit HUD (speed, gear, GPS, ADAS). */
class HudViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY changed)
    Q_PROPERTY(int gear READ gear NOTIFY changed)
    Q_PROPERTY(QString gpsLine READ gpsLine NOTIFY changed)
    Q_PROPERTY(double headingDeg READ headingDeg NOTIFY changed)
    Q_PROPERTY(double accelerationMps2 READ accelerationMps2 NOTIFY changed)
    Q_PROPERTY(bool adasActive READ adasActive NOTIFY changed)

public:
    explicit HudViewModel(QObject* parent = nullptr);

    double speedKmh() const { return speedKmh_; }
    int gear() const { return gear_; }
    QString gpsLine() const { return gpsLine_; }
    double headingDeg() const { return headingDeg_; }
    double accelerationMps2() const { return accelerationMps2_; }
    bool adasActive() const { return adasActive_; }

    void applySnapshot(const CarlaWorldSnapshot& snapshot);

signals:
    void changed();

private:
    double speedKmh_{0.0};
    int gear_{3};
    QString gpsLine_;
    double headingDeg_{0.0};
    double accelerationMps2_{0.0};
    bool adasActive_{true};
};

}  // namespace sdv::dash::carla::integration
