#pragma once

#include <QFrame>
#include <QJsonObject>

class QLabel;

namespace sdv::dash::mapbox {
class MiniMapView;
}

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::ui {

/** Bottom row: ADAS | NAVI (minimap + turn-by-turn) | EV */
class CockpitInstrumentDock : public QFrame {
    Q_OBJECT
public:
    explicit CockpitInstrumentDock(QWidget* parent = nullptr);

    sdv::dash::mapbox::MiniMapView* miniMap() const { return miniMap_; }

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void updateFromCarState(const QJsonObject& carState, bool mqttConnected);
    void setPygameMapCockpitMode(bool enabled);

private:
    void onEgoChanged();

    sdv::dash::mapbox::MiniMapView* miniMap_{nullptr};
    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    QWidget* gearRowWidget_{nullptr};

    QLabel* speedValue_{nullptr};
    QLabel* adasState_{nullptr};
    QLabel* routeLine_{nullptr};
    QLabel* maneuverDist_{nullptr};
    QLabel* maneuverHint_{nullptr};
    QLabel* mapHint_{nullptr};
    QLabel* gearP_{nullptr};
    QLabel* gearR_{nullptr};
    QLabel* gearN_{nullptr};
    QLabel* gearD_{nullptr};
    QLabel* evSoc_{nullptr};
    QLabel* evRange_{nullptr};
    QLabel* evTemp_{nullptr};
    QLabel* odo_{nullptr};
    QLabel* ready_{nullptr};
};

}  // namespace sdv::dash::ui
