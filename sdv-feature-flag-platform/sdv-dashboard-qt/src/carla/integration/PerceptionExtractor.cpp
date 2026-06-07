#include "carla/integration/PerceptionExtractor.h"

#include <QDateTime>

namespace sdv::dash::carla::integration {

sdv::dash::ops::PerceptionObjectList PerceptionExtractor::extract(const CarlaWorldSnapshot& snapshot)
{
    sdv::dash::ops::PerceptionObjectList list;
    list.stampNs = snapshot.vehicle.stampNs;
    list.frameId = snapshot.vehicle.frameId;
    list.objects.reserve(snapshot.actors.size());

    for (const auto& actor : snapshot.actors) {
        sdv::dash::ops::PerceptionObject obj;
        obj.objectId = actor.objectId;
        obj.classification = actor.classification;
        obj.existenceProbability = actor.existenceProbability;
        obj.x = actor.x;
        obj.y = actor.y;
        obj.z = actor.z;
        obj.yawRad = actor.yawRad;
        obj.lengthM = actor.lengthM;
        obj.widthM = actor.widthM;
        obj.heightM = actor.heightM;
        obj.velocityXMps = actor.velocityXMps;
        obj.velocityYMps = actor.velocityYMps;
        list.objects.push_back(std::move(obj));
    }

    if (list.stampNs <= 0) {
        list.stampNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    }
    return list;
}

}  // namespace sdv::dash::carla::integration
