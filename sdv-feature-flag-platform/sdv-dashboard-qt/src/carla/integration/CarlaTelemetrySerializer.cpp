#include "carla/integration/CarlaTelemetrySerializer.h"

#include <QBuffer>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sdv::dash::carla::integration {

namespace {

QByteArray wrapEnvelope(const QString& type, qint64 stampNs, const QString& frameId, const QJsonObject& data)
{
    const QJsonObject root{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("type"), type},
        {QStringLiteral("stamp_ns"), stampNs},
        {QStringLiteral("stamp_iso"),
         QDateTime::fromMSecsSinceEpoch(stampNs / 1000000, Qt::UTC).toString(Qt::ISODate)},
        {QStringLiteral("frame_id"), frameId},
        {QStringLiteral("data"), data},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

}  // namespace

QByteArray CarlaTelemetrySerializer::bridgeHello()
{
    return wrapEnvelope(
        QStringLiteral("bridge_hello"),
        QDateTime::currentMSecsSinceEpoch() * 1000000LL,
        QString(),
        QJsonObject{{QStringLiteral("service"), QStringLiteral("carla-integration-v1")}});
}

QByteArray CarlaTelemetrySerializer::vehicleState(const CarlaVehicleSnapshot& vehicle)
{
    const QJsonObject data{
        {QStringLiteral("stamp_ns"), vehicle.stampNs},
        {QStringLiteral("frame_id"), QStringLiteral("base_link")},
        {QStringLiteral("longitudinal_velocity_mps"), vehicle.longitudinalVelocityMps},
        {QStringLiteral("lateral_velocity_mps"), vehicle.lateralVelocityMps},
        {QStringLiteral("heading_rate_rps"), vehicle.headingRateRps},
        {QStringLiteral("speed_mps"), vehicle.speedMps},
        {QStringLiteral("steering_angle_deg"), vehicle.steeringAngleDeg},
        {QStringLiteral("throttle"), vehicle.throttle},
        {QStringLiteral("brake"), vehicle.brake},
        {QStringLiteral("gear"), vehicle.gear},
    };
    return wrapEnvelope(QStringLiteral("vehicle_state"), vehicle.stampNs, QStringLiteral("base_link"), data);
}

QByteArray CarlaTelemetrySerializer::egoPose(const CarlaVehicleSnapshot& vehicle)
{
    const double cy = qCos(vehicle.yawRad * 0.5);
    const double sy = qSin(vehicle.yawRad * 0.5);
    const QJsonObject data{
        {QStringLiteral("stamp_ns"), vehicle.stampNs},
        {QStringLiteral("frame_id"), vehicle.frameId},
        {QStringLiteral("position"),
         QJsonObject{
             {QStringLiteral("x"), vehicle.x},
             {QStringLiteral("y"), vehicle.y},
             {QStringLiteral("z"), vehicle.z},
         }},
        {QStringLiteral("orientation"),
         QJsonObject{
             {QStringLiteral("w"), cy},
             {QStringLiteral("x"), 0.0},
             {QStringLiteral("y"), 0.0},
             {QStringLiteral("z"), sy},
         }},
        {QStringLiteral("yaw_rad"), vehicle.yawRad},
        {QStringLiteral("speed_mps"), vehicle.speedMps},
    };
    return wrapEnvelope(QStringLiteral("ego_pose"), vehicle.stampNs, vehicle.frameId, data);
}

QByteArray CarlaTelemetrySerializer::trajectory(const sdv::dash::ops::Trajectory& trajectory)
{
    QJsonArray points;
    for (const auto& wp : trajectory.points) {
        points.append(QJsonObject{
            {QStringLiteral("x"), wp.x},
            {QStringLiteral("y"), wp.y},
            {QStringLiteral("z"), wp.z},
            {QStringLiteral("yaw_rad"), wp.yawRad},
            {QStringLiteral("longitudinal_velocity_mps"), wp.longitudinalVelocityMps},
            {QStringLiteral("time_from_start_sec"), wp.timeFromStartSec},
        });
    }
    const QJsonObject data{
        {QStringLiteral("stamp_ns"), trajectory.stampNs},
        {QStringLiteral("frame_id"), trajectory.frameId},
        {QStringLiteral("points"), points},
    };
    return wrapEnvelope(QStringLiteral("trajectory"), trajectory.stampNs, trajectory.frameId, data);
}

QByteArray CarlaTelemetrySerializer::perceptionObjects(const sdv::dash::ops::PerceptionObjectList& objects)
{
    QJsonArray arr;
    for (const auto& obj : objects.objects) {
        arr.append(QJsonObject{
            {QStringLiteral("object_id"), obj.objectId},
            {QStringLiteral("classification"), obj.classification},
            {QStringLiteral("existence_probability"), obj.existenceProbability},
            {QStringLiteral("position"),
             QJsonObject{
                 {QStringLiteral("x"), obj.x},
                 {QStringLiteral("y"), obj.y},
                 {QStringLiteral("z"), obj.z},
             }},
            {QStringLiteral("yaw_rad"), obj.yawRad},
            {QStringLiteral("dimensions_m"),
             QJsonObject{
                 {QStringLiteral("length"), obj.lengthM},
                 {QStringLiteral("width"), obj.widthM},
                 {QStringLiteral("height"), obj.heightM},
             }},
            {QStringLiteral("velocity_mps"),
             QJsonObject{
                 {QStringLiteral("x"), obj.velocityXMps},
                 {QStringLiteral("y"), obj.velocityYMps},
             }},
        });
    }
    const QJsonObject data{
        {QStringLiteral("stamp_ns"), objects.stampNs},
        {QStringLiteral("frame_id"), objects.frameId},
        {QStringLiteral("objects"), arr},
    };
    return wrapEnvelope(QStringLiteral("perception_objects"), objects.stampNs, objects.frameId, data);
}

QByteArray CarlaTelemetrySerializer::frontCamera(const QImage& image, const qint64 stampNs, const QString& frameId)
{
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 88);
    const QJsonObject data{
        {QStringLiteral("encoding"), QStringLiteral("base64")},
        {QStringLiteral("format"), QStringLiteral("jpeg")},
        {QStringLiteral("width"), image.width()},
        {QStringLiteral("height"), image.height()},
        {QStringLiteral("payload"), QString::fromLatin1(jpeg.toBase64())},
    };
    return wrapEnvelope(QStringLiteral("front_camera"), stampNs, frameId, data);
}

QVector<QByteArray> CarlaTelemetrySerializer::snapshotToTelemetryFrames(
    const CarlaWorldSnapshot& snapshot,
    const sdv::dash::ops::Trajectory& laneTrajectory,
    const sdv::dash::ops::PerceptionObjectList& perception)
{
    QVector<QByteArray> frames;
    frames.reserve(6);
    frames.append(vehicleState(snapshot.vehicle));
    frames.append(egoPose(snapshot.vehicle));
    if (laneTrajectory.isValid()) {
        frames.append(trajectory(laneTrajectory));
    }
    if (perception.isValid()) {
        frames.append(perceptionObjects(perception));
    }
    if (snapshot.hasCamera && !snapshot.camera.isNull()) {
        frames.append(frontCamera(snapshot.camera, snapshot.vehicle.stampNs, QStringLiteral("carla_rgb_front")));
    }
    return frames;
}

}  // namespace sdv::dash::carla::integration
