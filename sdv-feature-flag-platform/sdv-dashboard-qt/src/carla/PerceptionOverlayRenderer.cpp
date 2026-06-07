#include "carla/PerceptionOverlayRenderer.h"

#include "carla/CameraProjection.h"
#include "carla/FsdPaintKit.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/domain/OpsTypes.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/PerceptionViewModel.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtMath>

#include <algorithm>

namespace sdv::dash::carla {

namespace {

using namespace sdv::dash::carla::fsd;

enum class ObjectKind { Vehicle, Pedestrian, Bicycle, Other };

ObjectKind kindForClass(const QString& cls)
{
    const QString c = cls.toLower();
    if (c.contains(QStringLiteral("ped"))) {
        return ObjectKind::Pedestrian;
    }
    if (c.contains(QStringLiteral("cycl")) || c.contains(QStringLiteral("bike"))) {
        return ObjectKind::Bicycle;
    }
    if (c.contains(QStringLiteral("vehicle")) || c.contains(QStringLiteral("car")) || c.contains(QStringLiteral("truck"))) {
        return ObjectKind::Vehicle;
    }
    if (c.contains(QStringLiteral("traffic_light"))) {
        return ObjectKind::Other;
    }
    return ObjectKind::Other;
}

QColor colorForKind(const ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::Pedestrian:
        return FsdPalette::pedestrianBox();
    case ObjectKind::Bicycle:
        return FsdPalette::bicycleBox();
    case ObjectKind::Vehicle:
        return FsdPalette::vehicleBox();
    default:
        return QColor(148, 163, 184, 210);
    }
}

QString kindShortLabel(const ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::Pedestrian:
        return QStringLiteral("PED");
    case ObjectKind::Bicycle:
        return QStringLiteral("BIKE");
    case ObjectKind::Vehicle:
        return QStringLiteral("VEH");
    default:
        return QStringLiteral("OBJ");
    }
}

bool projectObjectBox(
    const CameraProjection& proj,
    const sdv::dash::ops::EgoPose& ego,
    const sdv::dash::ops::PerceptionObject& obj,
    int viewW,
    int viewH,
    QRectF* outBox,
    QPointF* groundCenter,
    double* outForward,
    double* outDist)
{
    const double c = qCos(obj.yawRad);
    const double s = qSin(obj.yawRad);
    const double hx = qMax(1.2, obj.lengthM * 0.5);
    const double hy = qMax(0.6, obj.widthM * 0.5);

    const QPointF corners[4] = {
        proj.project(ego, obj.x + c * hx - s * hy, obj.y + s * hx + c * hy, obj.z, viewW, viewH),
        proj.project(ego, obj.x + c * hx + s * hy, obj.y + s * hx - c * hy, obj.z, viewW, viewH),
        proj.project(ego, obj.x - c * hx + s * hy, obj.y - s * hx - c * hy, obj.z, viewW, viewH),
        proj.project(ego, obj.x - c * hx - s * hy, obj.y - s * hx + c * hy, obj.z, viewW, viewH),
    };

    double minX = 1e9;
    double minY = 1e9;
    double maxX = -1e9;
    double maxY = -1e9;
    int visible = 0;
    double sumX = 0;
    double sumY = 0;
    for (const QPointF& pt : corners) {
        if (!proj.isVisible(pt)) {
            continue;
        }
        ++visible;
        minX = qMin(minX, pt.x());
        minY = qMin(minY, pt.y());
        maxX = qMax(maxX, pt.x());
        maxY = qMax(maxY, pt.y());
        sumX += pt.x();
        sumY += pt.y();
    }
    if (visible < 2) {
        return false;
    }

    const double dx = obj.x - ego.x;
    const double dy = obj.y - ego.y;
    const double ec = qCos(-ego.yawRad);
    const double es = qSin(-ego.yawRad);
    const double forward = dx * ec - dy * es;
    const double dist = qSqrt(dx * dx + dy * dy);

    if (outForward) {
        *outForward = forward;
    }
    if (outDist) {
        *outDist = dist;
    }
    if (groundCenter) {
        *groundCenter = QPointF(sumX / visible, maxY);
    }

    *outBox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    return true;
}

}  // namespace

PerceptionOverlayRenderer::PerceptionOverlayRenderer(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void PerceptionOverlayRenderer::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(pipeline_->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, &PerceptionOverlayRenderer::onTelemetryChanged);
    connect(
        pipeline_->perceptionViewModel(),
        &sdv::dash::ops::PerceptionViewModel::changed,
        this,
        &PerceptionOverlayRenderer::onTelemetryChanged);
    onTelemetryChanged();
}

void PerceptionOverlayRenderer::onTelemetryChanged()
{
    update();
}

void PerceptionOverlayRenderer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!pipeline_) {
        return;
    }

    const sdv::dash::ops::EgoPose ego = pipeline_->egoViewModel()->pose();
    const auto objects = pipeline_->perceptionViewModel()->objects().objects;
    if (!ego.isValid() || objects.isEmpty()) {
        return;
    }

    const CameraProjection proj;
    const QPointF egoAnchor(width() * 0.5, height() * 0.92);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    struct DrawItem {
        QRectF box;
        QPointF ground;
        QColor color;
        QString primary;
        QString secondary;
        ObjectKind kind{ObjectKind::Other};
        bool isAcc{false};
        double forward{0};
    };
    QVector<DrawItem> items;
    items.reserve(objects.size());

    int accIndex = -1;
    double accForward = 1e9;

    for (const auto& obj : objects) {
        if (obj.classification.contains(QStringLiteral("traffic_light"), Qt::CaseInsensitive)) {
            continue;
        }
        QRectF box;
        QPointF ground;
        double forward = 0;
        double dist = 0;
        if (!projectObjectBox(proj, ego, obj, width(), height(), &box, &ground, &forward, &dist)) {
            continue;
        }

        const ObjectKind kind = kindForClass(obj.classification);
        QColor col = colorForKind(kind);
        const qreal scale = depthScale(forward);
        col.setAlpha(static_cast<int>(col.alpha() * (0.55 + 0.45 * scale)));

        const double objSpeed = qSqrt(obj.velocityXMps * obj.velocityXMps + obj.velocityYMps * obj.velocityYMps);
        const double relKmh = (objSpeed - ego.speedMps) * 3.6;

        const QString primary = QStringLiteral("%1  %2 m").arg(kindShortLabel(kind)).arg(dist, 0, 'f', 0);
        const QString secondary =
            QStringLiteral("Δ %1%2 km/h").arg(relKmh >= 0 ? QStringLiteral("+") : QString()).arg(relKmh, 0, 'f', 0);

        items.append({box, ground, col, primary, secondary, kind, false, forward});

        if (kind == ObjectKind::Vehicle && forward > 4.0 && forward < accForward) {
            const double dx = obj.x - ego.x;
            const double dy = obj.y - ego.y;
            const double ec = qCos(-ego.yawRad);
            const double es = qSin(-ego.yawRad);
            const double lateral = dx * es + dy * ec;
            if (qAbs(lateral) < 2.5) {
                accForward = forward;
                accIndex = items.size() - 1;
            }
        }
    }

    if (accIndex >= 0) {
        items[accIndex].isAcc = true;
    }

    std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) { return a.forward > b.forward; });

    for (const DrawItem& item : items) {
        const qreal corner = 10.0 + 18.0 * depthScale(item.forward);
        const qreal lineW = 1.6 + 1.4 * depthScale(item.forward);

        drawLeaderLine(p, egoAnchor, item.ground, item.color);

        drawGroundDiamond(p, item.ground, 12.0 + 8.0 * depthScale(item.forward), item.color);

        if (item.isAcc) {
            QRectF accBox = item.box.adjusted(-4, -4, 4, 4);
            p.setPen(QPen(FsdPalette::accHighlight(), 3.5));
            p.setBrush(QColor(251, 191, 36, 30));
            p.drawRoundedRect(accBox, 5, 5);

            const QRectF tag(accBox.left(), accBox.top() - 24, 56, 18);
            drawLabelChip(p, tag, QStringLiteral("ACC TARGET"), FsdPalette::accHighlight());
        }

        drawTeslaCornerBox(p, item.box, item.color, corner, lineW);

        const QRectF labelRect(item.box.left(), item.box.top() - 22, qMin(200.0, item.box.width() + 72), 18);
        drawLabelChip(p, labelRect, item.primary, item.color);

        const QRectF subRect(labelRect.left(), labelRect.bottom() + 2, labelRect.width(), 14);
        QFont sub = p.font();
        sub.setPixelSize(9);
        sub.setBold(true);
        p.setFont(sub);
        p.setPen(item.color);
        p.drawText(subRect.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, item.secondary);

        if (item.kind == ObjectKind::Vehicle && !item.secondary.isEmpty()) {
            const QPointF velAnchor(item.box.center().x(), item.box.center().y());
            const double rel = item.secondary.mid(2).toDouble();
            const QPointF velEnd = velAnchor + QPointF(qBound(-22.0, rel * 1.8, 22.0), 0);
            p.setPen(QPen(item.color, 2.0));
            p.drawLine(velAnchor, velEnd);
            p.drawEllipse(velEnd, 2.5, 2.5);
        }
    }
}

}  // namespace sdv::dash::carla
