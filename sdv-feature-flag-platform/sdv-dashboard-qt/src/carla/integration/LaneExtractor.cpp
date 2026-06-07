#include "carla/integration/LaneExtractor.h"

#include <QtMath>

namespace sdv::dash::carla::integration {

sdv::dash::ops::Trajectory LaneExtractor::extract(const CarlaWorldSnapshot& snapshot)
{
    sdv::dash::ops::Trajectory trajectory;
    const auto& v = snapshot.vehicle;
    trajectory.stampNs = v.stampNs;
    trajectory.frameId = v.frameId;

    if (!snapshot.routePoints.isEmpty()) {
        trajectory.points.reserve(snapshot.routePoints.size());
        for (const auto& rp : snapshot.routePoints) {
            sdv::dash::ops::TrajectoryWaypoint wp;
            wp.x = rp.x;
            wp.y = rp.y;
            wp.z = rp.z;
            wp.yawRad = rp.yawRad;
            wp.longitudinalVelocityMps = v.speedMps;
            trajectory.points.push_back(wp);
        }
        return trajectory;
    }

    constexpr int pointCount = 56;
    constexpr double aheadM = 95.0;
    constexpr double laneHalf = 1.85;

    const double c = qCos(v.yawRad);
    const double s = qSin(v.yawRad);

    trajectory.points.reserve(pointCount);
    for (int i = 0; i < pointCount; ++i) {
        const double t = static_cast<double>(i) / (pointCount - 1);
        const double d = aheadM * t;
        const double curve = 0.08 * qSin(t * M_PI * 1.2);
        const double lateral = curve * laneHalf;

        sdv::dash::ops::TrajectoryWaypoint wp;
        wp.x = v.x + d * c - lateral * s;
        wp.y = v.y + d * s + lateral * c;
        wp.z = v.z;
        wp.yawRad = v.yawRad + curve * 0.12;
        wp.longitudinalVelocityMps = v.speedMps;
        wp.timeFromStartSec = d / qMax(1.0, v.speedMps);
        trajectory.points.push_back(wp);
    }

    return trajectory;
}

}  // namespace sdv::dash::carla::integration
