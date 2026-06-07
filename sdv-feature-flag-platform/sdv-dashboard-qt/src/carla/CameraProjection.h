#pragma once

#include <QPointF>
#include <QtMath>

#include "ops/domain/OpsTypes.h"

namespace sdv::dash::carla {

/** Pinhole projection from map-frame points into a front-camera style viewport. */
struct CameraProjection {
    double focalPx{520.0};
    double horizonYRatio{0.58};
    double cameraHeightM{1.35};

    QPointF project(
        const sdv::dash::ops::EgoPose& ego,
        double worldX,
        double worldY,
        double worldZ,
        int viewWidth,
        int viewHeight) const
    {
        if (!ego.isValid() || viewWidth < 8 || viewHeight < 8) {
            return QPointF(-1.0, -1.0);
        }

        const double dx = worldX - ego.x;
        const double dy = worldY - ego.y;
        const double dz = worldZ - ego.z;
        const double c = qCos(-ego.yawRad);
        const double s = qSin(-ego.yawRad);
        const double forward = dx * c - dy * s;
        const double lateral = dx * s + dy * c;
        const double up = cameraHeightM - dz;

        if (forward < 2.5) {
            return QPointF(-1.0, -1.0);
        }

        const double scale = static_cast<double>(viewWidth) / 1280.0;
        const double fx = focalPx * scale;
        const double fy = focalPx * scale;
        const double cx = viewWidth * 0.5;
        const double cy = viewHeight * horizonYRatio;

        const double u = cx + (lateral / forward) * fx;
        const double v = cy - (up / forward) * fy;
        if (u < -80.0 || u > viewWidth + 80.0 || v < -80.0 || v > viewHeight + 80.0) {
            return QPointF(-1.0, -1.0);
        }
        return QPointF(u, v);
    }

    bool isVisible(const QPointF& p) const { return p.x() >= 0.0 && p.y() >= 0.0; }
};

}  // namespace sdv::dash::carla
