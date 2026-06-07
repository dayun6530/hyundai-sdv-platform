#pragma once

#include <QObject>
#include <QString>

namespace sdv::dash::mapbox {

class MapboxBridge : public QObject {
    Q_OBJECT
public:
    explicit MapboxBridge(QObject* parent = nullptr);

    Q_INVOKABLE QString mapboxToken() const { return mapboxToken_; }
    Q_INVOKABLE QString mapboxTokenWarning() const { return tokenWarning_; }
    void setMapboxToken(const QString& token);
    /** WGS84 origin for local map-frame x/y (meters) → lng/lat projection. */
    Q_INVOKABLE double mapOriginLng() const { return originLng_; }
    Q_INVOKABLE double mapOriginLat() const { return originLat_; }

    /** first_person | third_person | dashboard | operations */
    Q_PROPERTY(QString cameraMode READ cameraMode WRITE setCameraMode NOTIFY cameraModeChanged)
    Q_INVOKABLE QString cameraMode() const { return cameraMode_; }
    Q_INVOKABLE void setCameraMode(const QString& mode);

    Q_INVOKABLE void updateEgoPose(
        double x, double y, double z, double yawRad, double speedMps, const QString& frameId);
    Q_INVOKABLE void updateTrajectory(const QString& geoJsonLineString);
    Q_INVOKABLE void updatePerception(const QString& geoJsonFeatureCollection);

    /** Called from cockpit map JS when route bearing / turn distance changes. */
    Q_INVOKABLE void publishNavGuidance(const QString& maneuverArrows, const QString& distanceText);
    /** Shift+click on minimap → local map coordinates (meters). */
    Q_INVOKABLE void destinationPicked(double localX, double localY);
    Q_INVOKABLE void updatePlannedRoute(const QString& geoJsonFeature);

public slots:
    void notifyTokenReady();

signals:
    void mapboxTokenReady(const QString& token);
    void egoPoseChanged(
        double x, double y, double z, double yawRad, double speedMps, const QString& frameId);
    void trajectoryChanged(const QString& geoJsonLineString);
    void perceptionChanged(const QString& geoJsonFeatureCollection);
    void cameraModeChanged(const QString& mode);
    void navGuidanceChanged(const QString& maneuverArrows, const QString& distanceText);
    void destinationPickedLocal(double x, double y);
    void plannedRouteChanged(const QString& geoJsonFeature);

private:
    QString mapboxToken_;
    QString tokenWarning_;
    QString cameraMode_{QStringLiteral("first_person")};
    double originLng_ = 127.0276;
    double originLat_ = 37.4979;
};

}  // namespace sdv::dash::mapbox
