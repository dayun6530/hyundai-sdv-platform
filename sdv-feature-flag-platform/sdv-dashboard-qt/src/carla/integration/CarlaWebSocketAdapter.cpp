#include "carla/integration/CarlaWebSocketAdapter.h"

#include "twin/TwinSnapshotParser.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
#include <QWebSocket>
#endif

namespace sdv::dash::carla::integration {

namespace {

CarlaActorSnapshot parseActor(const QJsonObject& o)
{
    CarlaActorSnapshot a;
    a.objectId = o.value(QStringLiteral("object_id")).toString();
    a.classification = o.value(QStringLiteral("classification")).toString();
    const QJsonObject pos = o.value(QStringLiteral("position")).toObject();
    a.x = pos.value(QStringLiteral("x")).toDouble(o.value(QStringLiteral("x")).toDouble());
    a.y = pos.value(QStringLiteral("y")).toDouble(o.value(QStringLiteral("y")).toDouble());
    a.z = pos.value(QStringLiteral("z")).toDouble(o.value(QStringLiteral("z")).toDouble());
    a.yawRad = o.value(QStringLiteral("yaw_rad")).toDouble();
    const QJsonObject dim = o.value(QStringLiteral("dimensions_m")).toObject();
    a.lengthM = dim.value(QStringLiteral("length")).toDouble(o.value(QStringLiteral("length_m")).toDouble(4.5));
    a.widthM = dim.value(QStringLiteral("width")).toDouble(o.value(QStringLiteral("width_m")).toDouble(2.0));
    a.heightM = dim.value(QStringLiteral("height")).toDouble(o.value(QStringLiteral("height_m")).toDouble(1.5));
    const QJsonObject vel = o.value(QStringLiteral("velocity_mps")).toObject();
    a.velocityXMps = vel.value(QStringLiteral("x")).toDouble();
    a.velocityYMps = vel.value(QStringLiteral("y")).toDouble();
    a.existenceProbability = o.value(QStringLiteral("existence_probability")).toDouble(0.9);
    return a;
}

}  // namespace

CarlaWebSocketAdapter::CarlaWebSocketAdapter(QObject* parent) : CarlaApiAdapter(parent)
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(socket_, &QWebSocket::connected, this, &CarlaWebSocketAdapter::onConnected);
    connect(socket_, &QWebSocket::disconnected, this, &CarlaWebSocketAdapter::onDisconnected);
    connect(socket_, &QWebSocket::textMessageReceived, this, &CarlaWebSocketAdapter::onTextMessage);
#endif
}

CarlaWebSocketAdapter::~CarlaWebSocketAdapter() = default;

void CarlaWebSocketAdapter::setEndpoint(const QUrl& url)
{
    if (url.isValid()) {
        endpoint_ = url;
    }
}

void CarlaWebSocketAdapter::start()
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    if (!socket_) {
        return;
    }
    emit statusMessage(QStringLiteral("CARLA WS adapter connecting %1").arg(endpoint_.toString()));
    socket_->open(endpoint_);
#else
    emit statusMessage(QStringLiteral("CARLA WS adapter unavailable (Qt WebSockets missing)"));
#endif
}

void CarlaWebSocketAdapter::stop()
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->close();
    }
#endif
    connected_ = false;
    emit runningChanged(false);
}

bool CarlaWebSocketAdapter::isRunning() const
{
    return connected_;
}

QString CarlaWebSocketAdapter::statusLine() const
{
    return connected_ ? QStringLiteral("CARLA WS · connected")
                      : QStringLiteral("CARLA WS · waiting for %1").arg(endpoint_.toString());
}

void CarlaWebSocketAdapter::onConnected()
{
    connected_ = true;
    emit runningChanged(true);
    emit statusMessage(QStringLiteral("CARLA WS adapter connected"));
}

void CarlaWebSocketAdapter::onDisconnected()
{
    connected_ = false;
    emit runningChanged(false);
    emit statusMessage(QStringLiteral("CARLA WS adapter disconnected"));
}

void CarlaWebSocketAdapter::onTextMessage(const QString& message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("type")).toString() != QLatin1String("carla_snapshot")) {
        return;
    }
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    const QJsonObject vehicle = data.value(QStringLiteral("vehicle")).toObject();

    CarlaWorldSnapshot snap;
    snap.vehicle.stampNs = root.value(QStringLiteral("stamp_ns")).toVariant().toLongLong();
    snap.vehicle.frameId = vehicle.value(QStringLiteral("frame_id")).toString(QStringLiteral("map"));
    const QJsonObject pos = vehicle.value(QStringLiteral("position")).toObject();
    snap.vehicle.x = pos.value(QStringLiteral("x")).toDouble();
    snap.vehicle.y = pos.value(QStringLiteral("y")).toDouble();
    snap.vehicle.z = pos.value(QStringLiteral("z")).toDouble();
    snap.vehicle.yawRad = vehicle.value(QStringLiteral("yaw_rad")).toDouble();
    snap.vehicle.speedMps = vehicle.value(QStringLiteral("speed_mps")).toDouble();
    snap.vehicle.longitudinalVelocityMps = vehicle.value(QStringLiteral("longitudinal_velocity_mps")).toDouble(snap.vehicle.speedMps);
    snap.vehicle.lateralVelocityMps = vehicle.value(QStringLiteral("lateral_velocity_mps")).toDouble();
    snap.vehicle.headingRateRps = vehicle.value(QStringLiteral("heading_rate_rps")).toDouble();
    snap.vehicle.steeringAngleDeg = vehicle.value(QStringLiteral("steering_angle_deg")).toDouble();
    snap.vehicle.throttle = vehicle.value(QStringLiteral("throttle")).toDouble();
    snap.vehicle.brake = vehicle.value(QStringLiteral("brake")).toDouble();
    snap.vehicle.gear = vehicle.value(QStringLiteral("gear")).toInt(3);
    snap.vehicle.adasActive = vehicle.value(QStringLiteral("adas_active")).toBool(true);

    const QJsonObject gnss = data.value(QStringLiteral("gnss")).toObject();
    snap.gnss.latitudeDeg = gnss.value(QStringLiteral("latitude_deg")).toDouble();
    snap.gnss.longitudeDeg = gnss.value(QStringLiteral("longitude_deg")).toDouble();
    snap.gnss.altitudeM = gnss.value(QStringLiteral("altitude_m")).toDouble();

    const QJsonObject imu = data.value(QStringLiteral("imu")).toObject();
    snap.imu.accelXMps2 = imu.value(QStringLiteral("accel_x_mps2")).toDouble();
    snap.imu.accelYMps2 = imu.value(QStringLiteral("accel_y_mps2")).toDouble();
    snap.imu.accelZMps2 = imu.value(QStringLiteral("accel_z_mps2")).toDouble();

    const QJsonArray actors = data.value(QStringLiteral("actors")).toArray();
    snap.actors.reserve(actors.size());
    for (const auto& v : actors) {
        snap.actors.append(parseActor(v.toObject()));
    }

    snap.twin = sdv::dash::twin::TwinSnapshotParser::parseTwinMetadata(data);

    const QJsonArray routePts = data.value(QStringLiteral("trajectory_points")).toArray();
    snap.routePoints.reserve(routePts.size());
    for (const auto& v : routePts) {
        const QJsonObject o = v.toObject();
        CarlaRoutePoint rp;
        rp.x = o.value(QStringLiteral("x")).toDouble();
        rp.y = o.value(QStringLiteral("y")).toDouble();
        rp.z = o.value(QStringLiteral("z")).toDouble();
        rp.yawRad = o.value(QStringLiteral("yaw_rad")).toDouble();
        snap.routePoints.append(rp);
    }

    const QString camB64 = data.value(QStringLiteral("camera_jpeg_b64")).toString();
    if (!camB64.isEmpty()) {
        QImage img;
        if (img.loadFromData(QByteArray::fromBase64(camB64.toLatin1()), "JPEG")) {
            snap.camera = img;
            snap.hasCamera = true;
        }
    }

    emit snapshotReady(snap);
}

}  // namespace sdv::dash::carla::integration
