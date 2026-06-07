#include "twin/DigitalTwinController.h"

#include "carla/CarlaCockpitView.h"
#include "carla/HudOverlayWidget.h"
#include "carla/NavigationOverlayRenderer.h"
#include "carla/TrafficLightOverlayRenderer.h"
#include "carla/control/CarlaControlManager.h"
#include "carla/integration/CarlaApiAdapter.h"
#include "carla/integration/CarlaDataBridge.h"
#include "carla/integration/CarlaTypes.h"
#include "carla/integration/CarlaWebSocketAdapter.h"
#include "mapbox/MapboxBridge.h"
#include "ops/OpsTelemetryPipeline.h"
#include "twin/TwinSnapshotParser.h"
#include "twin/adapters/CarlaTwinAdapters.h"

#include <QGridLayout>
#include <QJsonArray>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

namespace sdv::dash::twin {

DigitalTwinController::DigitalTwinController(QObject* parent) : QObject(parent)
{
    carla_ = new CarlaTwinAdapters(this);
}

void DigitalTwinController::configure(
    const QString& carlaMode,
    sdv::dash::carla::integration::CarlaDataBridge* bridge,
    sdv::dash::carla::control::CarlaControlManager* control,
    sdv::dash::mapbox::MapboxBridge* mapbox,
    sdv::dash::carla::CarlaCockpitView* cockpit,
    sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    mapbox_ = mapbox;
    cockpit_ = cockpit;
    pipeline_ = pipeline;
    active_ = !carlaMode.trimmed().isEmpty() && carlaMode != QLatin1String("off");

    const QString mode = carlaMode.trimmed().toLower();

    if (carla_) {
        carla_->setControlManager(control);
    }

    if (cockpit_ && !trafficOverlay_) {
        trafficOverlay_ = new sdv::dash::carla::TrafficLightOverlayRenderer(cockpit_);
        if (auto* grid = qobject_cast<QGridLayout*>(cockpit_->layout())) {
            grid->addWidget(trafficOverlay_, 0, 0, 1, 1);
        }
        if (pipeline_) {
            trafficOverlay_->bindPipeline(pipeline_);
        }
    }
    if (trafficOverlay_) {
        const bool pygameBridge =
            mode == QLatin1String("bridge") || mode == QLatin1String("external");
        trafficOverlay_->setVisible(!pygameBridge);
    }

    if (bridge) {
        connect(bridge, &sdv::dash::carla::integration::CarlaDataBridge::twinSnapshotReady, this,
                &DigitalTwinController::onSnapshot);
    }

    if (mode == QLatin1String("bridge") || mode == QLatin1String("external")) {
        auto* ws = new sdv::dash::carla::integration::CarlaWebSocketAdapter(this);
        ws->setEndpoint(QUrl(QStringLiteral("ws://127.0.0.1:9092")));
        connect(ws, &sdv::dash::carla::integration::CarlaApiAdapter::snapshotReady, this,
                &DigitalTwinController::onSnapshot);
        ws->start();
        twinSnapAdapter_ = ws;
    }

    if (mapbox_) {
        connect(mapbox_, &sdv::dash::mapbox::MapboxBridge::destinationPickedLocal, this,
                &DigitalTwinController::onDestinationPicked);
    }

    connect(carla_, &CarlaTwinAdapters::worldStateChanged, this, &DigitalTwinController::onWorldState);

    emit statusMessage(active_ ? QStringLiteral("SDV Digital Twin active")
                                 : QStringLiteral("SDV Digital Twin standby"));
}

void DigitalTwinController::onSnapshot(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap)
{
    if (!active_) {
        return;
    }
    TwinWorldState twin = snap.twin;
    carla_->ingestSnapshot(snap, twin);
    if (trafficOverlay_) {
        trafficOverlay_->setTrafficLights(twin.trafficLights);
    }
    pushRouteToMap(snap);
}

void DigitalTwinController::onDestinationPicked(const double x, const double y)
{
    if (carla_) {
        carla_->setDestination(x, y);
    }
    emit statusMessage(QStringLiteral("Destination set: %1, %2").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
}

void DigitalTwinController::onWorldState(const TwinWorldState& state)
{
    if (!cockpit_ || !cockpit_->hudOverlay()) {
        return;
    }
    auto* hud = cockpit_->hudOverlay();
    hud->setDriveModeBadge(state.autonomy.runState == QLatin1String("driving")
                              ? QStringLiteral("AUTONOMOUS")
                              : state.autonomy.runState);
    hud->updateTwinNavigation(
        state.navigation.maneuverArrow,
        QStringLiteral("%1 m").arg(qRound(state.navigation.distanceToGoalM)),
        state.navigation.etaSec,
        state.navigation.routeProgress,
        state.speedLimitKmh);
    if (cockpit_->navigationOverlay()) {
        cockpit_->navigationOverlay()->setNavGuidance(
            state.navigation.maneuverArrow,
            QStringLiteral("%1 m").arg(qRound(state.navigation.distanceToGoalM)));
    }
}

void DigitalTwinController::pushRouteToMap(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap)
{
    if (!mapbox_) {
        return;
    }
    const QString geo = trajectoryToGeoJson(snap, mapbox_);
    if (!geo.isEmpty()) {
        mapbox_->updatePlannedRoute(geo);
    }
}

QString DigitalTwinController::trajectoryToGeoJson(
    const sdv::dash::carla::integration::CarlaWorldSnapshot& snap,
    sdv::dash::mapbox::MapboxBridge* mapbox) const
{
    if (!mapbox || snap.routePoints.isEmpty()) {
        return {};
    }
    QJsonArray coords;
    const double oLng = mapbox->mapOriginLng();
    const double oLat = mapbox->mapOriginLat();
    const double metersPerDegLat = 111320.0;
    const double metersPerDegLng = 111320.0 * qCos(oLat * M_PI / 180.0);

    for (const auto& pt : snap.routePoints) {
        const double lng = oLng + pt.x / metersPerDegLng;
        const double lat = oLat + pt.y / metersPerDegLat;
        coords.append(QJsonArray{lng, lat});
    }
    const QJsonObject feature{
        {QStringLiteral("type"), QStringLiteral("Feature")},
        {QStringLiteral("geometry"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("LineString")},
             {QStringLiteral("coordinates"), coords},
         }},
    };
    return QString::fromUtf8(QJsonDocument(feature).toJson(QJsonDocument::Compact));
}

}  // namespace sdv::dash::twin
