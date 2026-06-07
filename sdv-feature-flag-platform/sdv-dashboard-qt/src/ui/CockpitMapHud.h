#pragma once

#include <QFrame>
#include <QJsonObject>

class QLabel;

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::ui {

/** In-cabin HUD drawn over the 3D map (speed / nav glance). */
class CockpitMapHud : public QFrame {
    Q_OBJECT
public:
    explicit CockpitMapHud(QWidget* parent = nullptr);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void updateFromCarState(const QJsonObject& carState, bool mqttConnected);
    void updateNavGuidance(const QString& maneuverArrows, const QString& distanceText);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onEgoChanged();

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    QLabel* speedValue_{nullptr};
    QLabel* speedLimit_{nullptr};
    QLabel* maneuverArrows_{nullptr};
    QLabel* maneuverDist_{nullptr};
    QLabel* odo_{nullptr};
    QLabel* gearD_{nullptr};

    double speedKmh_{0.0};
};

}  // namespace sdv::dash::ui
