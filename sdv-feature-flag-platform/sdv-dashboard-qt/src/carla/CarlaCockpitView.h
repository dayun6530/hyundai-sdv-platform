#pragma once



#include <QImage>

#include <QJsonObject>

#include <QUrl>

#include <QWidget>



class QLabel;
class QFrame;
class QMouseEvent;
class QTimer;



namespace sdv::dash::mapbox {

class MapboxBridge;

class MiniMapView;

}



namespace sdv::dash::ops {

class OpsTelemetryPipeline;

}



namespace sdv::dash::carla {



class CarlaCameraReceiver;

class CinematicOverlay;

class HudOverlayWidget;

class LaneOverlayRenderer;

class NavigationOverlayRenderer;

class PerceptionOverlayRenderer;



/**

 * Main driver-view cockpit: CARLA RGB front camera + telemetry HUD overlays.

 * MiniMapView (Mapbox) stays in the top-right corner.

 */

class CarlaCockpitView : public QWidget {

    Q_OBJECT

public:

    explicit CarlaCockpitView(QWidget* parent = nullptr);

    ~CarlaCockpitView() override;



    sdv::dash::mapbox::MiniMapView* miniMap() const { return miniMap_; }



    void setMapboxBridge(sdv::dash::mapbox::MapboxBridge* bridge);

    void setTelemetryWebSocketUrl(const QUrl& url);

    void setVehicleId(const QString& vehicleId);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);

    void updateFromCarState(const QJsonObject& carState, bool mqttConnected);

    /** RGB frame from CarlaDataBridge (CARLA Integration v1). */
    void ingestCameraFrame(const QImage& frame);

    /** Hide FSD/HUD overlays; show only pygame map (no_rendering_mode) in main view. */
    void setPygameMapCockpitMode(bool enabled);

    /** Use RosBridgeClient for camera JSON (single WS); do not open a second socket. */
    void setPipelineCameraOnly(bool enabled);

    HudOverlayWidget* hudOverlay() const { return hudOverlay_; }
    NavigationOverlayRenderer* navigationOverlay() const { return navigationOverlay_; }

signals:

    void cameraFramePresented();



protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;



private slots:

    void onCameraFrame(const QImage& frame);

    void flushPendingCameraFrame();

    void onNavGuidance(const QString& arrows, const QString& distanceText);

    void layoutOverlays();

    void pushMiniMapTelemetry();



private:

    QLabel* videoLabel_{nullptr};

    QFrame* miniMapChrome_{nullptr};

    sdv::dash::mapbox::MiniMapView* miniMap_{nullptr};

    LaneOverlayRenderer* laneOverlay_{nullptr};

    NavigationOverlayRenderer* navigationOverlay_{nullptr};

    PerceptionOverlayRenderer* perceptionOverlay_{nullptr};

    CinematicOverlay* cinematicOverlay_{nullptr};

    HudOverlayWidget* hudOverlay_{nullptr};

    CarlaCameraReceiver* camera_{nullptr};

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};

    sdv::dash::mapbox::MapboxBridge* mapboxBridge_{nullptr};

    QUrl wsUrl_{QStringLiteral("ws://127.0.0.1:9090")};

    QImage lastFrame_;

    bool fadedIn_{false};
    bool pygameMapCockpit_{false};
    bool pipelineCameraOnly_{false};
    QImage pendingCameraFrame_;
    QTimer* cameraUiTimer_{nullptr};

};



}  // namespace sdv::dash::carla

