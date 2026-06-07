#include "carla/TrafficLightOverlayRenderer.h"

#include "carla/CameraProjection.h"
#include "carla/FsdPaintKit.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/PerceptionViewModel.h"

#include <QPainter>
#include <QPaintEvent>

namespace sdv::dash::carla {

TrafficLightOverlayRenderer::TrafficLightOverlayRenderer(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void TrafficLightOverlayRenderer::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(
        pipeline_->perceptionViewModel(),
        &sdv::dash::ops::PerceptionViewModel::changed,
        this,
        &TrafficLightOverlayRenderer::onPerceptionChanged);
}

void TrafficLightOverlayRenderer::setTrafficLights(const QVector<sdv::dash::twin::TwinTrafficLight>& lights)
{
    lights_ = lights;
    update();
}

void TrafficLightOverlayRenderer::onPerceptionChanged()
{
    update();
}

void TrafficLightOverlayRenderer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!pipeline_) {
        return;
    }
    const sdv::dash::ops::EgoPose ego = pipeline_->egoViewModel()->pose();
    if (!ego.isValid()) {
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const CameraProjection proj;

    auto drawLight = [&](double x, double y, double z, const QString& state) {
        const QPointF pt = proj.project(ego, x, y, z, width(), height());
        if (!proj.isVisible(pt)) {
            return;
        }
        QColor fill(100, 116, 139, 200);
        if (state == QLatin1String("red")) {
            fill = QColor(239, 68, 68, 230);
        } else if (state == QLatin1String("yellow")) {
            fill = QColor(250, 204, 21, 230);
        } else if (state == QLatin1String("green")) {
            fill = QColor(74, 222, 128, 230);
        }
        p.setPen(QPen(QColor(248, 250, 252, 180), 2.0));
        p.setBrush(fill);
        p.drawEllipse(pt, 10, 10);
    };

    for (const auto& tl : lights_) {
        drawLight(tl.x, tl.y, tl.z, tl.state);
    }

    const auto& list = pipeline_->perceptionViewModel()->objects();
    for (const auto& obj : list.objects) {
        if (!obj.classification.contains(QStringLiteral("traffic_light"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString state = obj.trafficLightState.trimmed().toLower();
        drawLight(obj.x, obj.y, obj.z, state.isEmpty() ? QStringLiteral("unknown") : state);
    }
}

}  // namespace sdv::dash::carla
