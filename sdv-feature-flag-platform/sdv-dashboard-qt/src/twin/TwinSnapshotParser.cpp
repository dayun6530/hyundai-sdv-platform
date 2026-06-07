#include "twin/TwinSnapshotParser.h"

#include <QJsonArray>

namespace sdv::dash::twin {

TwinWorldState TwinSnapshotParser::parseTwinMetadata(const QJsonObject& data)
{
    TwinWorldState twin;
    twin.speedLimitKmh = data.value(QStringLiteral("speed_limit_kmh")).toDouble(0.0);

    const QJsonArray lights = data.value(QStringLiteral("traffic_lights")).toArray();
    twin.trafficLights.reserve(lights.size());
    for (const auto& v : lights) {
        const QJsonObject o = v.toObject();
        TwinTrafficLight tl;
        tl.id = o.value(QStringLiteral("id")).toString();
        tl.state = o.value(QStringLiteral("state")).toString(QStringLiteral("unknown"));
        const QJsonObject pos = o.value(QStringLiteral("position")).toObject();
        tl.x = pos.value(QStringLiteral("x")).toDouble();
        tl.y = pos.value(QStringLiteral("y")).toDouble();
        tl.z = pos.value(QStringLiteral("z")).toDouble();
        tl.yawRad = o.value(QStringLiteral("yaw_rad")).toDouble();
        twin.trafficLights.append(tl);
    }

    const QJsonObject nav = data.value(QStringLiteral("navigation")).toObject();
    twin.navigation.maneuverArrow = nav.value(QStringLiteral("arrow")).toString(QStringLiteral("↑"));
    twin.navigation.maneuverText = nav.value(QStringLiteral("maneuver")).toString();
    twin.navigation.distanceToGoalM = nav.value(QStringLiteral("remain_m")).toDouble();
    twin.navigation.etaSec = nav.value(QStringLiteral("eta_sec")).toDouble();
    twin.navigation.routeProgress = nav.value(QStringLiteral("progress")).toDouble();

    const QJsonObject autonomy = data.value(QStringLiteral("autonomy")).toObject();
    twin.autonomy.agentMode = autonomy.value(QStringLiteral("mode")).toString(QStringLiteral("off"));
    twin.autonomy.runState = autonomy.value(QStringLiteral("state")).toString(QStringLiteral("idle"));
    twin.autonomy.maneuver = autonomy.value(QStringLiteral("maneuver")).toString();

    twin.valid = true;
    return twin;
}

void TwinSnapshotParser::mergeTrafficLightsIntoSnapshot(
    sdv::dash::carla::integration::CarlaWorldSnapshot& snap,
    const TwinWorldState& twin)
{
    for (const auto& tl : twin.trafficLights) {
        sdv::dash::carla::integration::CarlaActorSnapshot a;
        a.objectId = tl.id;
        a.classification = QStringLiteral("traffic_light");
        a.x = tl.x;
        a.y = tl.y;
        a.z = tl.z;
        a.yawRad = tl.yawRad;
        a.lengthM = 0.8;
        a.widthM = 0.8;
        a.heightM = 2.5;
        a.existenceProbability = 0.99;
        snap.actors.append(a);
    }
}

}  // namespace sdv::dash::twin
