#pragma once

#include <QFrame>
#include <QJsonObject>

class QLabel;

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla {

/** Glassmorphism HUD over the CARLA driver camera (telemetry from existing ViewModels). */
class HudOverlayWidget : public QFrame {
    Q_OBJECT
public:
    explicit HudOverlayWidget(QWidget* parent = nullptr);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void setVehicleId(const QString& vehicleId);
    void updateFromCarState(const QJsonObject& carState, bool mqttConnected);
    void updateNavGuidance(const QString& maneuverArrows, const QString& distanceText);
    void updateControlTelemetry(double throttle, double brake, double steer, bool autopilot, bool emergencyStop);
    void setDriveModeBadge(const QString& modeLabel);
    void updateTwinNavigation(const QString& arrow,
                              const QString& distanceText,
                              double etaSec,
                              double routeProgress,
                              double speedLimitKmh);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onEgoChanged();
    void refreshGpsLine();
    void updateGearFromPipeline(double speedMps);

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    QString vehicleId_{QStringLiteral("car_1")};
    QLabel* speedValue_{nullptr};
    QLabel* speedLimit_{nullptr};
    QLabel* adasState_{nullptr};
    QLabel* autoDrive_{nullptr};
    QLabel* maneuverArrows_{nullptr};
    QLabel* maneuverDist_{nullptr};
    QLabel* maneuverHint_{nullptr};
    QLabel* gearP_{nullptr};
    QLabel* gearR_{nullptr};
    QLabel* gearN_{nullptr};
    QLabel* gearD_{nullptr};
    QLabel* gpsLine_{nullptr};
    QLabel* controlLine_{nullptr};
    QLabel* headingLine_{nullptr};
    double speedKmh_{0.0};
    double headingDeg_{0.0};
    bool adasActive_{true};
    bool autoActive_{true};
    int activeGear_{3};
};

}  // namespace sdv::dash::carla
