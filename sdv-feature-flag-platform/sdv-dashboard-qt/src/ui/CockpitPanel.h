#pragma once



#include <QJsonObject>

#include <QLabel>

#include <QWidget>

#include <QUrl>



namespace sdv::dash::mapbox {

class MapboxBridge;

class MapboxCockpitView;

class MiniMapView;

}



namespace sdv::dash::carla {

class CarlaCockpitView;

}



namespace sdv::dash::ops {

class OpsTelemetryPipeline;

}



namespace sdv::dash::ui {

class CockpitHudOverlay;

class CockpitInstrumentDock;

class CockpitMapHud;

}



namespace sdv::dash::ui {



class CockpitPanel : public QWidget {

    Q_OBJECT

public:

    explicit CockpitPanel(QWidget* parent = nullptr);



    sdv::dash::carla::CarlaCockpitView* carlaView() const { return carlaView_; }

    sdv::dash::mapbox::MiniMapView* miniMap() const;

    CockpitHudOverlay* hud() const { return hud_; }

    CockpitInstrumentDock* instrumentDock() const { return instrumentDock_; }



    void setMapboxBridge(sdv::dash::mapbox::MapboxBridge* bridge);

    void setTelemetryWebSocketUrl(const QUrl& url);

    void setVehicleId(const QString& vehicleId);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);

    void updateFromCarState(const QJsonObject& carState, bool mqttConnected);

    void setDriverViewChrome(bool visible);

    /** Show full Mapbox scene (Operations / Dashboard view modes). */
    void setFullMapVisible(bool visible);

    /** Main cockpit = pygame no_rendering_mode only (no SDV FSD overlays). */
    void setPygameMapCockpitMode(bool enabled);



signals:

    void vehicleSelected(const QString& carId);
    void cameraFramePresented();



private:

    sdv::dash::carla::CarlaCockpitView* carlaView_{nullptr};
    sdv::dash::mapbox::MapboxCockpitView* mapboxView_{nullptr};

    CockpitHudOverlay* hud_{nullptr};

    CockpitMapHud* mapHud_{nullptr};

    CockpitInstrumentDock* instrumentDock_{nullptr};

    QLabel* buildTag_{nullptr};

};



}  // namespace sdv::dash::ui

