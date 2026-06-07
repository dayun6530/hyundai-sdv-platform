#include "carla/control/CarlaVehicleController.h"

#include <QtMath>

namespace sdv::dash::carla::control {

namespace {

constexpr double kThrottleStep = 0.08;
constexpr double kBrakeStep = 0.12;
constexpr double kSteerStep = 0.06;
constexpr double kSteerDecay = 0.92;

}  // namespace

CarlaVehicleController::CarlaVehicleController(QObject* parent) : QObject(parent) {}

void CarlaVehicleController::setThrottle(const double v)
{
    throttle_ = qBound(0.0, v, 1.0);
    notify();
}

void CarlaVehicleController::setBrake(const double v)
{
    brake_ = qBound(0.0, v, 1.0);
    notify();
}

void CarlaVehicleController::setSteer(const double v)
{
    steer_ = qBound(-1.0, v, 1.0);
    notify();
}

void CarlaVehicleController::setHandBrake(const bool on)
{
    handBrake_ = on;
    notify();
}

void CarlaVehicleController::setReverse(const bool on)
{
    reverse_ = on;
    notify();
}

void CarlaVehicleController::setAutopilot(const bool on)
{
    autopilot_ = on;
    if (on) {
        emergencyStop_ = false;
        keyW_ = keyS_ = keyA_ = keyD_ = keySpace_ = false;
        throttle_ = brake_ = steer_ = 0.0;
        handBrake_ = false;
    }
    notify();
}

void CarlaVehicleController::setCameraSlot(const CameraSlot slot)
{
    cameraSlot_ = slot;
    notify();
}

void CarlaVehicleController::setDestination(const double x, const double y)
{
    destX_ = x;
    destY_ = y;
    hasDestination_ = true;
    notify();
}

void CarlaVehicleController::clearDestination()
{
    hasDestination_ = false;
    notify();
}

void CarlaVehicleController::setAutonomousAgent(const QString& mode)
{
    autonomousAgent_ = mode.trimmed().isEmpty() ? QStringLiteral("off") : mode.trimmed();
    notify();
}

void CarlaVehicleController::setAutonomousAction(const QString& action)
{
    autonomousAction_ = action.trimmed();
    notify();
}

void CarlaVehicleController::clearAutonomousAction()
{
    autonomousAction_.clear();
}

void CarlaVehicleController::emergencyStopNow()
{
    emergencyStop_ = true;
    autopilot_ = false;
    throttle_ = 0.0;
    brake_ = 1.0;
    steer_ = 0.0;
    handBrake_ = true;
    keyW_ = keyS_ = keyA_ = keyD_ = keySpace_ = false;
    notify();
}

void CarlaVehicleController::releaseEmergencyStop()
{
    emergencyStop_ = false;
    handBrake_ = false;
    brake_ = 0.0;
    notify();
}

void CarlaVehicleController::applyKeyDown(const int key)
{
    if (emergencyStop_ && key != Qt::Key_Escape) {
        return;
    }
    switch (key) {
    case Qt::Key_W:
        keyW_ = true;
        break;
    case Qt::Key_S:
        keyS_ = true;
        break;
    case Qt::Key_A:
        keyA_ = true;
        break;
    case Qt::Key_D:
        keyD_ = true;
        break;
    case Qt::Key_Space:
        keySpace_ = true;
        handBrake_ = true;
        break;
    case Qt::Key_R:
        reverse_ = !reverse_;
        if (reverse_) {
            keyW_ = false;
            throttle_ = 0.0;
        }
        notify();
        if (autopilot_) {
            return;
        }
        decayInputs();
        return;
    case Qt::Key_P:
        setAutopilot(!autopilot_);
        return;
    case Qt::Key_Escape:
        emergencyStopNow();
        return;
    default:
        break;
    }
    if (!autopilot_) {
        decayInputs();
    }
}

void CarlaVehicleController::applyKeyUp(const int key)
{
    switch (key) {
    case Qt::Key_W:
        keyW_ = false;
        break;
    case Qt::Key_S:
        keyS_ = false;
        break;
    case Qt::Key_A:
        keyA_ = false;
        break;
    case Qt::Key_D:
        keyD_ = false;
        break;
    case Qt::Key_Space:
        keySpace_ = false;
        if (!emergencyStop_) {
            handBrake_ = false;
        }
        break;
    default:
        return;
    }
    if (!autopilot_) {
        decayInputs();
    }
}

void CarlaVehicleController::decayInputs()
{
    if (autopilot_ || emergencyStop_) {
        return;
    }

    throttle_ = keyW_ ? qMin(1.0, throttle_ + kThrottleStep) : qMax(0.0, throttle_ - kThrottleStep * 1.4);
    brake_ = keyS_ ? qMin(1.0, brake_ + kBrakeStep) : qMax(0.0, brake_ - kBrakeStep * 1.4);

    double targetSteer = 0.0;
    if (keyA_) {
        targetSteer -= 1.0;
    }
    if (keyD_) {
        targetSteer += 1.0;
    }
    if (keyA_ || keyD_) {
        steer_ = qBound(-1.0, steer_ + (targetSteer - steer_) * 0.35, 1.0);
    } else {
        steer_ *= kSteerDecay;
        if (qAbs(steer_) < 0.02) {
            steer_ = 0.0;
        }
    }

    handBrake_ = keySpace_ || emergencyStop_;
    clampInputs();
    notify();
}

QString CarlaVehicleController::cameraSlotToString(const CameraSlot slot)
{
    switch (slot) {
    case CameraSlot::Rear:
        return QStringLiteral("rear");
    case CameraSlot::Left:
        return QStringLiteral("left");
    case CameraSlot::Right:
        return QStringLiteral("right");
    case CameraSlot::Front:
    default:
        return QStringLiteral("front");
    }
}

QJsonObject CarlaVehicleController::toCommandJson() const
{
    QJsonObject data;
    data.insert(QStringLiteral("throttle"), throttle_);
    data.insert(QStringLiteral("brake"), brake_);
    data.insert(QStringLiteral("steer"), steer_);
    data.insert(QStringLiteral("hand_brake"), handBrake_);
    data.insert(QStringLiteral("reverse"), reverse_);
    data.insert(QStringLiteral("autopilot"), autopilot_);
    data.insert(QStringLiteral("emergency_stop"), emergencyStop_);
    data.insert(QStringLiteral("camera"), cameraSlotToString(cameraSlot_));
    if (hasDestination_) {
        QJsonObject dest;
        dest.insert(QStringLiteral("x"), destX_);
        dest.insert(QStringLiteral("y"), destY_);
        data.insert(QStringLiteral("destination"), dest);
    }
    if (!autonomousAgent_.isEmpty() && autonomousAgent_ != QLatin1String("off")) {
        data.insert(QStringLiteral("autonomous_agent"), autonomousAgent_);
    }
    if (!autonomousAction_.isEmpty()) {
        data.insert(QStringLiteral("autonomous_action"), autonomousAction_);
    }
    QJsonObject root;
    root.insert(QStringLiteral("type"), QStringLiteral("carla_control"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("data"), data);
    return root;
}

void CarlaVehicleController::clampInputs()
{
    throttle_ = qBound(0.0, throttle_, 1.0);
    brake_ = qBound(0.0, brake_, 1.0);
    steer_ = qBound(-1.0, steer_, 1.0);
}

void CarlaVehicleController::notify()
{
    emit stateChanged();
}

}  // namespace sdv::dash::carla::control
