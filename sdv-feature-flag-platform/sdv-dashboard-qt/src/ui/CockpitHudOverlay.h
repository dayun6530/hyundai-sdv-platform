#pragma once

#include <QFrame>
#include <QJsonObject>
#include <QString>

class QComboBox;
class QLabel;

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::ui {

class CockpitHudOverlay : public QFrame {
    Q_OBJECT
public:
    explicit CockpitHudOverlay(QWidget* parent = nullptr);

    QComboBox* vehicleSelector() const { return vehicleCombo_; }

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void updateFromCarState(const QJsonObject& carState, bool mqttConnected);

signals:
    void vehicleSelected(const QString& carId);

private slots:
    void onEgoChanged();
    void onVehicleIndexChanged(int index);

private:
    QFrame* makeChip(QWidget* parent);
    void setMqttStatus(bool connected);

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    QComboBox* vehicleCombo_{nullptr};
    QLabel* mqttDot_{nullptr};
    QLabel* mqttText_{nullptr};
    QLabel* speedValue_{nullptr};
    QLabel* speedLimit_{nullptr};
    QLabel* adasState_{nullptr};
    QLabel* autoDrive_{nullptr};
    QLabel* routeLine_{nullptr};
    QLabel* maneuverDist_{nullptr};
    QLabel* maneuverHint_{nullptr};
    QLabel* gearP_{nullptr};
    QLabel* gearR_{nullptr};
    QLabel* gearN_{nullptr};
    QLabel* gearD_{nullptr};
    QLabel* evSoc_{nullptr};
    QLabel* evRange_{nullptr};
    QLabel* evTemp_{nullptr};
    QLabel* odo_{nullptr};
};

}  // namespace sdv::dash::ui
