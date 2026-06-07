#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace sdv::dash::carla::control {

/** Canonical vehicle control state (dashboard-side). */
class CarlaVehicleController : public QObject {
    Q_OBJECT
public:
    enum class CameraSlot { Front, Rear, Left, Right };

    explicit CarlaVehicleController(QObject* parent = nullptr);

    double throttle() const { return throttle_; }
    double brake() const { return brake_; }
    double steer() const { return steer_; }
    bool handBrake() const { return handBrake_; }
    bool reverse() const { return reverse_; }
    bool autopilot() const { return autopilot_; }
    bool emergencyStop() const { return emergencyStop_; }
    CameraSlot cameraSlot() const { return cameraSlot_; }
    double destinationX() const { return destX_; }
    double destinationY() const { return destY_; }
    bool hasDestination() const { return hasDestination_; }
    QString autonomousAction() const { return autonomousAction_; }
    QString autonomousAgent() const { return autonomousAgent_; }

    void setThrottle(double v);
    void setBrake(double v);
    void setSteer(double v);
    void setHandBrake(bool on);
    void setReverse(bool on);
    void setAutopilot(bool on);
    void setCameraSlot(CameraSlot slot);
    void setDestination(double x, double y);
    void clearDestination();

    void emergencyStopNow();
    void releaseEmergencyStop();

    void applyKeyDown(int key);
    void applyKeyUp(int key);
    void decayInputs();

    void setAutonomousAgent(const QString& mode);
    void setAutonomousAction(const QString& action);
    void clearAutonomousAction();

    QJsonObject toCommandJson() const;
    static QString cameraSlotToString(CameraSlot slot);

signals:
    void stateChanged();

private:
    void clampInputs();
    void notify();

    double throttle_{0.0};
    double brake_{0.0};
    double steer_{0.0};
    bool handBrake_{false};
    bool reverse_{false};
    bool autopilot_{false};
    bool emergencyStop_{false};
    CameraSlot cameraSlot_{CameraSlot::Front};
    double destX_{0.0};
    double destY_{0.0};
    bool hasDestination_{false};
    QString autonomousAgent_{QStringLiteral("off")};
    QString autonomousAction_;

    bool keyW_{false};
    bool keyS_{false};
    bool keyA_{false};
    bool keyD_{false};
    bool keySpace_{false};
};

}  // namespace sdv::dash::carla::control
