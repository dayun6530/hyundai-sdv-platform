#include "carla/LaneOverlayRenderer.h"

#include "carla/CameraProjection.h"
#include "carla/FsdPaintKit.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/TrajectoryViewModel.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

namespace sdv::dash::carla {

namespace {

using namespace sdv::dash::carla::fsd;

QPainterPath projectPath(
    const CameraProjection& proj,
    const sdv::dash::ops::EgoPose& ego,
    const QVector<sdv::dash::ops::TrajectoryWaypoint>& waypoints,
    int start,
    int end,
    int viewW,
    int viewH)
{
    QPainterPath path;
    bool first = true;
    const int last = qMin(end, waypoints.size());
    for (int i = qMax(0, start); i < last; ++i) {
        const auto& wp = waypoints.at(i);
        const QPointF pt = proj.project(ego, wp.x, wp.y, wp.z, viewW, viewH);
        if (!proj.isVisible(pt)) {
            continue;
        }
        if (first) {
            path.moveTo(pt);
            first = false;
        } else {
            path.lineTo(pt);
        }
    }
    return path;
}

void buildLanePaths(
    const CameraProjection& proj,
    const sdv::dash::ops::EgoPose& ego,
    const QVector<sdv::dash::ops::TrajectoryWaypoint>& waypoints,
    int viewW,
    int viewH,
    QPainterPath* left,
    QPainterPath* right,
    QPainterPath* center,
    QVector<QPointF>* leftPts,
    QVector<QPointF>* rightPts)
{
    const double laneHalf = 1.9;
    bool first = true;
    for (const auto& wp : waypoints) {
        const double yaw = wp.yawRad != 0.0 ? wp.yawRad : ego.yawRad;
        const double c = qCos(yaw);
        const double s = qSin(yaw);
        const double nx = -s;
        const double ny = c;

        const QPointF cpt = proj.project(ego, wp.x, wp.y, wp.z, viewW, viewH);
        const QPointF lpt = proj.project(ego, wp.x + nx * laneHalf, wp.y + ny * laneHalf, wp.z, viewW, viewH);
        const QPointF rpt = proj.project(ego, wp.x - nx * laneHalf, wp.y - ny * laneHalf, wp.z, viewW, viewH);

        if (!proj.isVisible(cpt) && !proj.isVisible(lpt) && !proj.isVisible(rpt)) {
            continue;
        }
        if (first) {
            if (proj.isVisible(cpt)) {
                center->moveTo(cpt);
            }
            if (proj.isVisible(lpt)) {
                left->moveTo(lpt);
                leftPts->append(lpt);
            }
            if (proj.isVisible(rpt)) {
                right->moveTo(rpt);
                rightPts->append(rpt);
            }
            first = false;
        } else {
            if (proj.isVisible(cpt)) {
                center->lineTo(cpt);
            }
            if (proj.isVisible(lpt)) {
                left->lineTo(lpt);
                leftPts->append(lpt);
            }
            if (proj.isVisible(rpt)) {
                right->lineTo(rpt);
                rightPts->append(rpt);
            }
        }
    }
}

}  // namespace

LaneOverlayRenderer::LaneOverlayRenderer(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void LaneOverlayRenderer::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(pipeline_->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, &LaneOverlayRenderer::onTelemetryChanged);
    connect(
        pipeline_->trajectoryViewModel(),
        &sdv::dash::ops::TrajectoryViewModel::changed,
        this,
        &LaneOverlayRenderer::onTelemetryChanged);
    onTelemetryChanged();
}

void LaneOverlayRenderer::onTelemetryChanged()
{
    update();
}

void LaneOverlayRenderer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!pipeline_) {
        return;
    }

    const sdv::dash::ops::EgoPose ego = pipeline_->egoViewModel()->pose();
    const auto waypoints = pipeline_->trajectoryViewModel()->waypoints();
    if (!ego.isValid() || waypoints.size() < 2) {
        return;
    }

    const CameraProjection proj;
    QPainterPath leftPath;
    QPainterPath rightPath;
    QPainterPath centerPath;
    QVector<QPointF> leftPts;
    QVector<QPointF> rightPts;
    buildLanePaths(proj, ego, waypoints, width(), height(), &leftPath, &rightPath, &centerPath, &leftPts, &rightPts);

    if (centerPath.isEmpty()) {
        return;
    }

    const int split = qMax(4, waypoints.size() / 3);
    const QPainterPath currentPath = projectPath(proj, ego, waypoints, 0, split, width(), height());
    const QPainterPath predictedPath = projectPath(proj, ego, waypoints, split, waypoints.size(), width(), height());

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (leftPts.size() >= 2 && rightPts.size() >= 2) {
        QPainterPath corridor;
        corridor.moveTo(leftPts.first());
        for (int i = 1; i < leftPts.size(); ++i) {
            corridor.lineTo(leftPts.at(i));
        }
        for (int i = rightPts.size() - 1; i >= 0; --i) {
            corridor.lineTo(rightPts.at(i));
        }
        corridor.closeSubpath();

        QLinearGradient fill(0, height() * 0.38, 0, height());
        fill.setColorAt(0.0, FsdPalette::corridorTop());
        fill.setColorAt(0.45, QColor(34, 211, 238, 45));
        fill.setColorAt(1.0, FsdPalette::corridorBottom());
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(corridor);
    }

    drawGlowPath(p, leftPath, QColor(147, 197, 253, 90), 18.0, 3);
    drawGlowPath(p, rightPath, QColor(147, 197, 253, 90), 18.0, 3);

    QPen boundaryPen(FsdPalette::laneLeft(), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(boundaryPen);
    p.drawPath(leftPath);
    p.drawPath(rightPath);

    QPen centerPen(FsdPalette::laneCenter(), 1.8, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
    centerPen.setDashPattern({6.0, 10.0});
    p.setPen(centerPen);
    p.drawPath(centerPath);

    if (!currentPath.isEmpty()) {
        drawGlowPath(p, currentPath, FsdPalette::pathCurrent(), 16.0, 2);
        QPen curPen(FsdPalette::pathCurrent(), 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(curPen);
        p.drawPath(currentPath);
    }

    if (!predictedPath.isEmpty()) {
        drawGlowPath(p, predictedPath, FsdPalette::pathPredicted(), 12.0, 2);
        QPen predPen(FsdPalette::pathPredicted(), 3.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        predPen.setDashPattern({5.0, 11.0});
        p.setPen(predPen);
        p.drawPath(predictedPath);
    }

    const int arrowIdx = qMin(split + 4, waypoints.size() - 1);
    const QPointF arrowBase = proj.project(ego, waypoints.at(qMax(0, arrowIdx - 2)).x, waypoints.at(qMax(0, arrowIdx - 2)).y,
                                          waypoints.at(qMax(0, arrowIdx - 2)).z, width(), height());
    const QPointF arrowTip = proj.project(ego, waypoints.at(arrowIdx).x, waypoints.at(arrowIdx).y, waypoints.at(arrowIdx).z,
                                        width(), height());
    if (proj.isVisible(arrowBase) && proj.isVisible(arrowTip)) {
        QPainterPath chevron;
        chevron.moveTo(arrowTip);
        chevron.lineTo(arrowBase + QPointF(-14, 10));
        chevron.lineTo(arrowBase + QPointF(14, 10));
        chevron.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(FsdPalette::pathPredicted());
        p.drawPath(chevron);
    }
}

}  // namespace sdv::dash::carla
