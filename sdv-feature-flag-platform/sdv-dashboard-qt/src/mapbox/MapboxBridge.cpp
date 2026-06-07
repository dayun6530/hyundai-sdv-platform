#include "mapbox/MapboxBridge.h"

#include "mapbox/MapboxTokenLoader.h"

#include <QProcessEnvironment>

namespace sdv::dash::mapbox {

MapboxBridge::MapboxBridge(QObject* parent) : QObject(parent)
{
    const MapboxTokenInfo resolved = resolveMapboxToken();
    mapboxToken_ = resolved.token;
    tokenWarning_ = resolved.warning;

    bool ok = false;
    const double lng = qEnvironmentVariable("MAP_ORIGIN_LNG").toDouble(&ok);
    if (ok) {
        originLng_ = lng;
    }
    ok = false;
    const double lat = qEnvironmentVariable("MAP_ORIGIN_LAT").toDouble(&ok);
    if (ok) {
        originLat_ = lat;
    }
}

void MapboxBridge::setMapboxToken(const QString& token)
{
    const MapboxTokenInfo resolved = resolveMapboxToken(token);
    if (resolved.token == mapboxToken_ && resolved.warning == tokenWarning_) {
        return;
    }
    mapboxToken_ = resolved.token;
    tokenWarning_ = resolved.warning;
    emit mapboxTokenReady(mapboxToken_);
}

void MapboxBridge::notifyTokenReady()
{
    emit mapboxTokenReady(mapboxToken_);
}

void MapboxBridge::updateEgoPose(
    const double x, const double y, const double z, const double yawRad, const double speedMps, const QString& frameId)
{
    emit egoPoseChanged(x, y, z, yawRad, speedMps, frameId);
}

void MapboxBridge::updateTrajectory(const QString& geoJsonLineString)
{
    if (geoJsonLineString.isEmpty()) {
        return;
    }
    emit trajectoryChanged(geoJsonLineString);
}

void MapboxBridge::updatePerception(const QString& geoJsonFeatureCollection)
{
    if (geoJsonFeatureCollection.isEmpty()) {
        return;
    }
    emit perceptionChanged(geoJsonFeatureCollection);
}

void MapboxBridge::publishNavGuidance(const QString& maneuverArrows, const QString& distanceText)
{
    emit navGuidanceChanged(maneuverArrows, distanceText);
}

void MapboxBridge::destinationPicked(const double localX, const double localY)
{
    emit destinationPickedLocal(localX, localY);
}

void MapboxBridge::updatePlannedRoute(const QString& geoJsonFeature)
{
    if (geoJsonFeature.isEmpty()) {
        return;
    }
    emit plannedRouteChanged(geoJsonFeature);
}

void MapboxBridge::setCameraMode(const QString& mode)
{
    QString normalized = mode.trimmed().toLower();
    if (normalized.contains(QStringLiteral("1st")) || normalized == QStringLiteral("first_person")) {
        normalized = QStringLiteral("first_person");
    } else if (normalized.contains(QStringLiteral("3rd")) || normalized == QStringLiteral("third_person")) {
        normalized = QStringLiteral("third_person");
    } else if (normalized.contains(QStringLiteral("dashboard"))) {
        normalized = QStringLiteral("dashboard");
    } else if (normalized.contains(QStringLiteral("operations"))) {
        normalized = QStringLiteral("operations");
    } else if (!normalized.isEmpty()) {
        normalized = QStringLiteral("first_person");
    } else {
        normalized = QStringLiteral("first_person");
    }
    if (normalized == cameraMode_) {
        return;
    }
    cameraMode_ = normalized;
    emit cameraModeChanged(cameraMode_);
}

}  // namespace sdv::dash::mapbox
