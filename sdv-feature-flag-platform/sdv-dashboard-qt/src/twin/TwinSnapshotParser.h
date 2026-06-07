#pragma once

#include "carla/integration/CarlaTypes.h"
#include "twin/TwinTypes.h"

#include <QJsonObject>

namespace sdv::dash::twin {

/** Parses twin metadata from Python carla_snapshot JSON (Phase A–C). */
class TwinSnapshotParser {
public:
    static TwinWorldState parseTwinMetadata(const QJsonObject& data);
    static void mergeTrafficLightsIntoSnapshot(sdv::dash::carla::integration::CarlaWorldSnapshot& snap,
                                               const TwinWorldState& twin);
};

}  // namespace sdv::dash::twin
