#include "carla/NavigationOverlayRenderer.h"

#include "carla/CameraProjection.h"
#include "carla/FsdPaintKit.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/TrajectoryViewModel.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QTimer>

namespace sdv::dash::carla {

namespace {

using namespace sdv::dash::carla::fsd;

QString primaryManeuver(const QString& arrows)
{
    const QString t = arrows.trimmed();
    if (t.contains(QChar(0x2190)) || t.contains(QLatin1String("←"))) {
        return QStringLiteral("←");
    }
    if (t.contains(QChar(0x2192)) || t.contains(QLatin1String("→"))) {
        return QStringLiteral("→");
    }
    if (t.contains(QLatin1String("U")) || t.contains(QLatin1String("u-turn"), Qt::CaseInsensitive)) {
        return QStringLiteral("↶");
    }
    return QStringLiteral("↑");
}

}  // namespace

NavigationOverlayRenderer::NavigationOverlayRenderer(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        navPulse_ += 0.06f;
        update();
    });
    timer->start(60);
}

void NavigationOverlayRenderer::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(pipeline_->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, &NavigationOverlayRenderer::onTelemetryChanged);
    connect(
        pipeline_->trajectoryViewModel(),
        &sdv::dash::ops::TrajectoryViewModel::changed,
        this,
        &NavigationOverlayRenderer::onTelemetryChanged);
    onTelemetryChanged();
}

void NavigationOverlayRenderer::setNavGuidance(const QString& maneuverArrows, const QString& distanceText)
{
    if (!maneuverArrows.isEmpty()) {
        navArrows_ = maneuverArrows;
    }
    if (!distanceText.isEmpty()) {
        navDistance_ = distanceText;
    }
    update();
}

void NavigationOverlayRenderer::onTelemetryChanged()
{
    update();
}

void NavigationOverlayRenderer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QString glyph = primaryManeuver(navArrows_);
    const float pulse = 0.5f + 0.5f * qSin(navPulse_);

    const int bannerW = 220;
    const int bannerH = 88;
    const QRect banner((width() - bannerW) / 2, 14, bannerW, bannerH);

    QRect glowRect = banner.adjusted(-6, -6, 6, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(34, 211, 238, static_cast<int>(25 + 20 * pulse)));
    p.drawRoundedRect(glowRect, 18, 18);

    QLinearGradient bannerBg(banner.topLeft(), banner.bottomLeft());
    bannerBg.setColorAt(0.0, QColor(8, 18, 40, 230));
    bannerBg.setColorAt(1.0, QColor(4, 12, 28, 210));
    p.setBrush(bannerBg);
    p.setPen(QPen(QColor(56, 189, 248, static_cast<int>(120 + 80 * pulse)), 2.0));
    p.drawRoundedRect(banner, 16, 16);

    const QRect arrowArea = banner.adjusted(0, 6, 0, -34);
    drawManeuverArrow(p, arrowArea, glyph, QColor(248, 250, 252));

    QFont sub = p.font();
    sub.setPixelSize(14);
    sub.setBold(true);
    p.setFont(sub);
    p.setPen(QColor(125, 211, 252));
    p.drawText(banner.adjusted(0, 52, 0, -8), Qt::AlignHCenter | Qt::AlignVCenter, navDistance_);

    if (!pipeline_) {
        return;
    }

    const sdv::dash::ops::EgoPose ego = pipeline_->egoViewModel()->pose();
    const auto waypoints = pipeline_->trajectoryViewModel()->waypoints();
    if (!ego.isValid() || waypoints.size() < 2) {
        return;
    }

    const CameraProjection proj;
    const double corridorHalf = 4.2;
    QPainterPath leftPath;
    QPainterPath rightPath;
    QVector<QPointF> leftPts;
    QVector<QPointF> rightPts;
    bool first = true;

    for (const auto& wp : waypoints) {
        const double yaw = wp.yawRad != 0.0 ? wp.yawRad : ego.yawRad;
        const double c = qCos(yaw);
        const double s = qSin(yaw);
        const double nx = -s;
        const double ny = c;

        const QPointF left = proj.project(ego, wp.x + nx * corridorHalf, wp.y + ny * corridorHalf, wp.z, width(), height());
        const QPointF right = proj.project(ego, wp.x - nx * corridorHalf, wp.y - ny * corridorHalf, wp.z, width(), height());
        if (!proj.isVisible(left) && !proj.isVisible(right)) {
            continue;
        }
        if (first) {
            if (proj.isVisible(left)) {
                leftPath.moveTo(left);
                leftPts.append(left);
            }
            if (proj.isVisible(right)) {
                rightPath.moveTo(right);
                rightPts.append(right);
            }
            first = false;
        } else {
            if (proj.isVisible(left)) {
                leftPath.lineTo(left);
                leftPts.append(left);
            }
            if (proj.isVisible(right)) {
                rightPath.lineTo(right);
                rightPts.append(right);
            }
        }
    }

    if (leftPts.size() < 2 || rightPts.size() < 2) {
        return;
    }

    QPainterPath corridor;
    corridor.moveTo(leftPts.first());
    for (int i = 1; i < leftPts.size(); ++i) {
        corridor.lineTo(leftPts.at(i));
    }
    for (int i = rightPts.size() - 1; i >= 0; --i) {
        corridor.lineTo(rightPts.at(i));
    }
    corridor.closeSubpath();

    QLinearGradient navFill(0, height() * 0.48, 0, height());
    navFill.setColorAt(0.0, QColor(37, 99, 235, 18));
    navFill.setColorAt(0.6, QColor(56, 189, 248, 42));
    navFill.setColorAt(1.0, QColor(34, 211, 238, 70));
    p.setPen(Qt::NoPen);
    p.setBrush(navFill);
    p.drawPath(corridor);

    QPen routePen(QColor(96, 165, 250, 140), 2.0, Qt::DashLine);
    routePen.setDashPattern({10.0, 12.0});
    p.setPen(routePen);
    p.drawPath(leftPath);
    p.drawPath(rightPath);
}

}  // namespace sdv::dash::carla
