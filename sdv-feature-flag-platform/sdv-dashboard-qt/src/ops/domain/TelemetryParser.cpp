#include "ops/domain/TelemetryParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtMath>

namespace sdv::dash::ops {

namespace {

double jsonDouble(const QJsonObject& obj, const char* key, const double fallback = 0.0)
{
    const auto value = obj.value(QLatin1String(key));
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

qint64 jsonInt64(const QJsonObject& obj, const char* key, const qint64 fallback = 0)
{
    const auto value = obj.value(QLatin1String(key));
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

}  // namespace

VehicleState VehicleState::fromJson(
    const QJsonObject& data, const qint64 envelopeStampNs, const QString& envelopeFrameId)
{
    VehicleState state;
    state.stampNs = jsonInt64(data, "stamp_ns", envelopeStampNs);
    state.frameId = data.value(QStringLiteral("frame_id")).toString(envelopeFrameId);
    state.longitudinalVelocityMps = jsonDouble(data, "longitudinal_velocity_mps");
    state.lateralVelocityMps = jsonDouble(data, "lateral_velocity_mps");
    state.headingRateRps = jsonDouble(data, "heading_rate_rps");
    state.speedMps = jsonDouble(data, "speed_mps",
        qHypot(state.longitudinalVelocityMps, state.lateralVelocityMps));
    return state;
}

EgoPose EgoPose::fromJson(const QJsonObject& data, const qint64 envelopeStampNs, const QString& envelopeFrameId)
{
    EgoPose pose;
    pose.stampNs = jsonInt64(data, "stamp_ns", envelopeStampNs);
    pose.frameId = data.value(QStringLiteral("frame_id")).toString(envelopeFrameId);
    pose.stampIso = data.value(QStringLiteral("stamp_iso")).toString();

    const QJsonObject position = data.value(QStringLiteral("position")).toObject();
    pose.x = jsonDouble(position, "x");
    pose.y = jsonDouble(position, "y");
    pose.z = jsonDouble(position, "z");

    const QJsonObject orientation = data.value(QStringLiteral("orientation")).toObject();
    pose.qw = jsonDouble(orientation, "w", 1.0);
    pose.qx = jsonDouble(orientation, "x");
    pose.qy = jsonDouble(orientation, "y");
    pose.qz = jsonDouble(orientation, "z");

    pose.yawRad = jsonDouble(data, "yaw_rad");
    pose.speedMps = jsonDouble(data, "speed_mps");
    return pose;
}

Trajectory Trajectory::fromJson(
    const QJsonObject& data, const qint64 envelopeStampNs, const QString& envelopeFrameId)
{
    Trajectory trajectory;
    trajectory.stampNs = jsonInt64(data, "stamp_ns", envelopeStampNs);
    trajectory.frameId = data.value(QStringLiteral("frame_id")).toString(envelopeFrameId);

    const QJsonArray points = data.value(QStringLiteral("points")).toArray();
    trajectory.points.reserve(points.size());
    for (const auto& value : points) {
        const QJsonObject pt = value.toObject();
        TrajectoryWaypoint wp;
        wp.x = jsonDouble(pt, "x");
        wp.y = jsonDouble(pt, "y");
        wp.z = jsonDouble(pt, "z");
        wp.yawRad = jsonDouble(pt, "yaw_rad");
        wp.longitudinalVelocityMps = jsonDouble(pt, "longitudinal_velocity_mps");
        wp.timeFromStartSec = jsonDouble(pt, "time_from_start_sec");
        trajectory.points.push_back(wp);
    }
    return trajectory;
}

PerceptionObjectList PerceptionObjectList::fromJson(
    const QJsonObject& data, const qint64 envelopeStampNs, const QString& envelopeFrameId)
{
    PerceptionObjectList list;
    list.stampNs = jsonInt64(data, "stamp_ns", envelopeStampNs);
    list.frameId = data.value(QStringLiteral("frame_id")).toString(envelopeFrameId);

    const QJsonArray objects = data.value(QStringLiteral("objects")).toArray();
    list.objects.reserve(objects.size());
    for (const auto& value : objects) {
        const QJsonObject obj = value.toObject();
        PerceptionObject out;
        out.objectId = obj.value(QStringLiteral("object_id")).toString();
        out.classification = obj.value(QStringLiteral("classification")).toString();
        out.existenceProbability = jsonDouble(obj, "existence_probability");

        const QJsonObject position = obj.value(QStringLiteral("position")).toObject();
        out.x = jsonDouble(position, "x");
        out.y = jsonDouble(position, "y");
        out.z = jsonDouble(position, "z");
        out.yawRad = jsonDouble(obj, "yaw_rad");

        const QJsonObject dimensions = obj.value(QStringLiteral("dimensions_m")).toObject();
        out.lengthM = jsonDouble(dimensions, "length");
        out.widthM = jsonDouble(dimensions, "width");
        out.heightM = jsonDouble(dimensions, "height");

        const QJsonObject velocity = obj.value(QStringLiteral("velocity_mps")).toObject();
        out.velocityXMps = jsonDouble(velocity, "x");
        out.velocityYMps = jsonDouble(velocity, "y");

        const QJsonObject attrs = obj.value(QStringLiteral("attributes")).toObject();
        out.trafficLightState = obj.value(QStringLiteral("tl_state"))
                                    .toString(attrs.value(QStringLiteral("tl_state")).toString())
                                    .trimmed()
                                    .toLower();

        list.objects.push_back(std::move(out));
    }
    return list;
}

std::optional<ParsedTelemetryEnvelope> TelemetryParser::parseFrame(const QByteArray& utf8Json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(utf8Json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();
    if (type.isEmpty()) {
        return std::nullopt;
    }

    ParsedTelemetryEnvelope envelope;
    envelope.schemaVersion = root.value(QStringLiteral("schema_version")).toInt(1);
    envelope.type = type;
    envelope.stampNs = jsonInt64(root, "stamp_ns");
    envelope.frameId = root.value(QStringLiteral("frame_id")).toString();
    envelope.data = root.value(QStringLiteral("data")).toObject();
    if (envelope.data.isEmpty() && type != QLatin1String("bridge_hello")) {
        return std::nullopt;
    }
    return envelope;
}

}  // namespace sdv::dash::ops
