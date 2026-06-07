#include "carla/integration/CarlaSimAdapter.h"

#include <QDateTime>
#include <QPainter>
#include <QtMath>

namespace sdv::dash::carla::integration {

namespace {

QImage syntheticCamera(const double t, const double speedKmh)
{
    QImage img(1280, 720, QImage::Format_RGB32);
    img.fill(QColor(12, 18, 32));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, 320);
    sky.setColorAt(0.0, QColor(30, 58, 95));
    sky.setColorAt(1.0, QColor(71, 85, 105));
    p.fillRect(0, 0, 1280, 320, sky);
    p.fillRect(0, 320, 1280, 400, QColor(55, 65, 81));

    for (int lane = -1; lane <= 1; ++lane) {
        const int cx = 640 + lane * 130 + static_cast<int>(20 * qSin(t));
        for (int y = 330; y < 700; y += 32) {
            p.setPen(QPen(QColor(248, 250, 252, 220), 3));
            p.drawLine(cx, y, cx, y + 16);
        }
    }

    p.setPen(QColor(74, 222, 128));
    QFont f = p.font();
    f.setPixelSize(22);
    f.setBold(true);
    p.setFont(f);
    p.drawText(24, 40, QStringLiteral("CARLA Integration v1 · Sim"));
    p.drawText(24, 680, QStringLiteral("%1 km/h · t=%2s").arg(qRound(speedKmh)).arg(t, 0, 'f', 1));
    p.end();
    return img;
}

}  // namespace

CarlaSimAdapter::CarlaSimAdapter(QObject* parent) : CarlaApiAdapter(parent)
{
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &CarlaSimAdapter::tick);
}

void CarlaSimAdapter::start()
{
    if (running_) {
        return;
    }
    running_ = true;
    phase_ = 0.0;
    timer_.start();
    emit runningChanged(true);
    emit statusMessage(QStringLiteral("CARLA sim adapter running"));
}

void CarlaSimAdapter::stop()
{
    timer_.stop();
    if (!running_) {
        return;
    }
    running_ = false;
    emit runningChanged(false);
    emit statusMessage(QStringLiteral("CARLA sim adapter stopped"));
}

QString CarlaSimAdapter::statusLine() const
{
    return running_ ? QStringLiteral("CARLA sim · live") : QStringLiteral("CARLA sim · stopped");
}

void CarlaSimAdapter::applyExternalState(const double x, const double y, const double z, const double yawRad,
                                         const double speedMps, const double steerDeg, const double throttle,
                                         const double brake, const int gear)
{
    useExternal_ = true;
    extX_ = x;
    extY_ = y;
    extZ_ = z;
    extYaw_ = yawRad;
    extSpeed_ = speedMps;
    extSteerDeg_ = steerDeg;
    extThrottle_ = throttle;
    extBrake_ = brake;
    extGear_ = gear;
}

void CarlaSimAdapter::tick()
{
    phase_ += 0.1;
    const qint64 stampNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;

    double speed = 12.0 + 2.0 * qSin(phase_ * 0.4);
    double yaw = phase_ * 0.08;
    double x = 100.0 + phase_ * 8.0 * qCos(yaw);
    double y = 200.0 + phase_ * 8.0 * qSin(yaw);
    double steerDeg = qSin(phase_) * 12.0;
    double throttle = 0.35;
    double brake = 0.0;
    int gear = 3;

    if (useExternal_) {
        x = extX_;
        y = extY_;
        yaw = extYaw_;
        speed = extSpeed_;
        steerDeg = extSteerDeg_;
        throttle = extThrottle_;
        brake = extBrake_;
        gear = extGear_;
    }

    CarlaWorldSnapshot snap;
    snap.vehicle.stampNs = stampNs;
    snap.vehicle.frameId = QStringLiteral("map");
    snap.vehicle.x = x;
    snap.vehicle.y = y;
    snap.vehicle.z = useExternal_ ? extZ_ : 0.5;
    snap.vehicle.yawRad = yaw;
    snap.vehicle.speedMps = qAbs(speed);
    snap.vehicle.longitudinalVelocityMps = speed;
    snap.vehicle.steeringAngleDeg = steerDeg;
    snap.vehicle.throttle = throttle;
    snap.vehicle.brake = brake;
    snap.vehicle.gear = gear;
    snap.vehicle.adasActive = true;

    snap.gnss.latitudeDeg = 37.5665 + y * 1e-6;
    snap.gnss.longitudeDeg = 126.9780 + x * 1e-6;
    snap.gnss.altitudeM = 35.0;

    snap.imu.accelXMps2 = 0.2 * qCos(phase_);
    snap.imu.accelYMps2 = 0.1 * qSin(phase_);

    const double c = qCos(yaw);
    const double s = qSin(yaw);
    snap.actors = {
        {QStringLiteral("sim_v1"),
         QStringLiteral("vehicle"),
         x + 22 * c,
         y + 22 * s,
         0.5,
         yaw,
         4.5,
         2.0,
         1.5,
         speed * 0.9,
         0.0,
         0.92},
        {QStringLiteral("sim_ped1"),
         QStringLiteral("pedestrian"),
         x + 10 * c - 4 * s,
         y + 10 * s + 4 * c,
         0.5,
         0.0,
         0.6,
         0.6,
         1.7,
         1.2,
         0.0,
         0.88},
        {QStringLiteral("sim_bike1"),
         QStringLiteral("cyclist"),
         x + 14 * c + 5 * s,
         y + 14 * s - 5 * c,
         0.5,
         yaw,
         1.8,
         0.7,
         1.5,
         4.0,
         0.0,
         0.85},
    };

    snap.camera = syntheticCamera(phase_, speed * 3.6);
    snap.hasCamera = true;

    emit snapshotReady(snap);
}

}  // namespace sdv::dash::carla::integration
