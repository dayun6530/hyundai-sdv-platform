#include "carla/control/CarlaControlManager.h"

#include "carla/CarlaCockpitView.h"
#include "carla/HudOverlayWidget.h"
#include "carla/NavigationOverlayRenderer.h"
#include "carla/control/CarlaControlBridge.h"
#include "carla/control/CarlaControlPanel.h"
#include "carla/control/CarlaVehicleController.h"
#include "carla/integration/CarlaSimAdapter.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QtMath>

namespace sdv::dash::carla::control {

struct CarlaControlManager::SimState {
    double x{100.0};
    double y{200.0};
    double z{0.5};
    double yaw{0.0};
    double speedMps{0.0};
    double navRemainM{0.0};
};

CarlaControlManager::CarlaControlManager(QObject* parent)
    : QObject(parent), controller_(new CarlaVehicleController(this)), bridge_(new CarlaControlBridge(this)),
      sim_(new SimState)
{
    bridge_->setController(controller_);
    connect(bridge_, &CarlaControlBridge::statusMessage, this, &CarlaControlManager::statusMessage);
    connect(bridge_, &CarlaControlBridge::navigationUpdate, this, &CarlaControlManager::navGuidanceChanged);
    connect(bridge_, &CarlaControlBridge::agentStatusUpdate, this,
            [this](const QString& mode, const QString& state, const QString& maneuver, const QString& detail) {
                emit agentStatusChanged(mode, state, maneuver);
                if (!detail.isEmpty()) {
                    emit statusMessage(detail);
                }
            });
    connect(controller_, &CarlaVehicleController::stateChanged, this, &CarlaControlManager::onControllerChanged);

    inputTimer_ = new QTimer(this);
    inputTimer_->setInterval(40);
    connect(inputTimer_, &QTimer::timeout, controller_, &CarlaVehicleController::decayInputs);

    simTimer_ = new QTimer(this);
    simTimer_->setInterval(50);
    connect(simTimer_, &QTimer::timeout, this, &CarlaControlManager::tickSim);
}

CarlaControlManager::~CarlaControlManager() = default;

void CarlaControlManager::configure(const QString& carlaModeCli,
                                    sdv::dash::carla::CarlaCockpitView* cockpit,
                                    sdv::dash::carla::HudOverlayWidget* hud,
                                    sdv::dash::carla::integration::CarlaSimAdapter* simAdapter,
                                    const QUrl& controlWsUrl,
                                    sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    simAdapter_ = simAdapter;
    pipeline_ = pipeline;
    hud_ = hud;
    attachCockpit(cockpit);

    const QString key = carlaModeCli.trimmed().toLower();
    bridgeEnabled_ = false;
    simControl_ = false;

    if (key == QLatin1String("sim") || key == QLatin1String("native-sim")) {
        consoleMode_ = ConsoleMode::Simulation;
        simControl_ = true;
        simTimer_->start();
        inputTimer_->start();
    } else if (key == QLatin1String("bridge") || key == QLatin1String("external") ||
               key == QLatin1String("carla-ws") || key == QLatin1String("native-carla-ws")) {
        consoleMode_ = ConsoleMode::CarlaLive;
        bridgeEnabled_ = true;
        bridge_->setWebSocketUrl(controlWsUrl);
        bridge_->setEnabled(true);
        inputTimer_->start();
    } else if (!key.isEmpty() && key != QLatin1String("off")) {
        consoleMode_ = ConsoleMode::Manual;
        inputTimer_->start();
    } else {
        consoleMode_ = ConsoleMode::Off;
        inputTimer_->stop();
        simTimer_->stop();
        bridge_->setEnabled(false);
    }

    if (panel_) {
        panel_->bindManager(this);
    }
    if (consoleMode_ != ConsoleMode::Off && qApp && !appDriveFilterInstalled_) {
        qApp->installEventFilter(this);
        appDriveFilterInstalled_ = true;
    }
    connect(this, &CarlaControlManager::navGuidanceChanged, this, [this](const QString& arrows, const QString& dist) {
        if (hud_) {
            hud_->updateNavGuidance(arrows, dist);
        }
        if (cockpit_ && cockpit_->navigationOverlay()) {
            cockpit_->navigationOverlay()->setNavGuidance(arrows, dist);
        }
    });
    refreshDestinationHint();
    updateDriveModeBadge();
    emit statusMessage(QStringLiteral("CARLA Control Center v2 · mode=%1").arg(key.isEmpty() ? QStringLiteral("off") : key));
}

void CarlaControlManager::refreshDestinationHint()
{
    QString hint;
    if (controller_->hasDestination()) {
        hint = QStringLiteral("DEST %1,%2")
                   .arg(controller_->destinationX(), 0, 'f', 0)
                   .arg(controller_->destinationY(), 0, 'f', 0);
    } else {
        hint = QStringLiteral("DEST: Shift+click minimap or ROUTE");
    }
    emit destinationHintChanged(hint);
}

void CarlaControlManager::attachCockpit(sdv::dash::carla::CarlaCockpitView* cockpit)
{
    if (cockpit_ == cockpit) {
        return;
    }
    if (cockpit_) {
        cockpit_->removeEventFilter(this);
    }
    cockpit_ = cockpit;
    if (!cockpit_) {
        return;
    }

    cockpit_->setFocusPolicy(Qt::StrongFocus);
    cockpit_->installEventFilter(this);

    if (!panel_) {
        panel_ = new CarlaControlPanel(cockpit_);
        panel_->bindManager(this);
        if (auto* grid = qobject_cast<QGridLayout*>(cockpit_->layout())) {
            grid->addWidget(panel_, 2, 0, 1, 1, Qt::AlignLeft | Qt::AlignBottom);
        }
    }
}

bool CarlaControlManager::isDriveKey(const int key)
{
    switch (key) {
    case Qt::Key_W:
    case Qt::Key_A:
    case Qt::Key_S:
    case Qt::Key_D:
    case Qt::Key_R:
    case Qt::Key_Space:
    case Qt::Key_P:
    case Qt::Key_Escape:
        return true;
    default:
        return false;
    }
}

bool CarlaControlManager::focusBlocksDriveKeys()
{
    QWidget* focus = QApplication::focusWidget();
    if (!focus) {
        return false;
    }
    return qobject_cast<const QLineEdit*>(focus) || qobject_cast<const QTextEdit*>(focus) ||
           qobject_cast<const QPlainTextEdit*>(focus);
}

bool CarlaControlManager::handleDriveKeyEvent(QEvent* event)
{
    if (consoleMode_ == ConsoleMode::Off || !controller_) {
        return false;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return false;
    }
    auto* ke = static_cast<QKeyEvent*>(event);
    if (ke->isAutoRepeat() || !isDriveKey(ke->key()) || focusBlocksDriveKeys()) {
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        controller_->applyKeyDown(ke->key());
        if (ke->key() == Qt::Key_R) {
            emit statusMessage(controller_->reverse() ? QStringLiteral("Gear: R (use W to back up, S to brake)")
                                                      : QStringLiteral("Gear: D"));
        }
        if (controller_->autopilot()) {
            consoleMode_ = ConsoleMode::Autonomous;
        } else if (bridgeEnabled_ || simControl_) {
            consoleMode_ = simControl_ ? ConsoleMode::Simulation : ConsoleMode::Manual;
        }
        updateDriveModeBadge();
    } else {
        controller_->applyKeyUp(ke->key());
        updateDriveModeBadge();
    }
    return true;
}

bool CarlaControlManager::eventFilter(QObject* watched, QEvent* event)
{
    if (handleDriveKeyEvent(event)) {
        return true;
    }
    if (watched != cockpit_ || consoleMode_ == ConsoleMode::Off) {
        return QObject::eventFilter(watched, event);
    }
    return QObject::eventFilter(watched, event);
}

void CarlaControlManager::setCameraSlot(const CarlaVehicleController::CameraSlot slot)
{
    controller_->setCameraSlot(slot);
}

void CarlaControlManager::toggleAutopilot()
{
    controller_->setAutopilot(!controller_->autopilot());
    consoleMode_ = controller_->autopilot() ? ConsoleMode::Autonomous
                                            : (simControl_ ? ConsoleMode::Simulation : ConsoleMode::Manual);
    updateDriveModeBadge();
}

void CarlaControlManager::triggerEmergencyStop()
{
    controller_->emergencyStopNow();
    consoleMode_ = ConsoleMode::Manual;
    updateDriveModeBadge();
}

bool CarlaControlManager::startAutonomousDriving(const QString& mode)
{
    const QString m = mode.trimmed().isEmpty() ? QStringLiteral("behavior") : mode.trimmed();
    if (!controller_->hasDestination()) {
        if (pipeline_) {
            const auto& pose = pipeline_->egoViewModel()->pose();
            if (pose.isValid()) {
                const double ahead = 80.0;
                controller_->setDestination(
                    pose.x + ahead * qCos(pose.yawRad), pose.y + ahead * qSin(pose.yawRad));
                emit statusMessage(QStringLiteral("AUTO: using default goal 80 m ahead"));
            } else {
                emit statusMessage(QStringLiteral("Set destination first (Shift+click minimap or ROUTE)"));
                refreshDestinationHint();
                return false;
            }
        } else if (!bridgeEnabled_ && !simControl_) {
            emit statusMessage(QStringLiteral("Set destination first (Shift+click minimap or ROUTE)"));
            refreshDestinationHint();
            return false;
        }
    }
    controller_->setAutopilot(true);
    controller_->setAutonomousAgent(m);
    controller_->setAutonomousAction(QStringLiteral("start"));
    bridge_->flushCommand();
    consoleMode_ = ConsoleMode::Autonomous;
    refreshDestinationHint();
    updateDriveModeBadge();
    emit agentStatusChanged(m, QStringLiteral("driving"), QStringLiteral("starting"));
    return true;
}

void CarlaControlManager::pauseAutonomousDriving()
{
    controller_->setAutonomousAction(QStringLiteral("pause"));
    bridge_->flushCommand();
    emit agentStatusChanged(controller_->autopilot() ? QStringLiteral("behavior") : QStringLiteral("off"),
                            QStringLiteral("paused"), QStringLiteral("paused"));
    updateDriveModeBadge();
}

void CarlaControlManager::resumeAutonomousDriving()
{
    controller_->setAutonomousAction(QStringLiteral("resume"));
    bridge_->flushCommand();
    consoleMode_ = ConsoleMode::Autonomous;
    emit agentStatusChanged(QStringLiteral("behavior"), QStringLiteral("driving"), QStringLiteral("lane_follow"));
    updateDriveModeBadge();
}

void CarlaControlManager::selectFleetVehicle(const QString& carId)
{
    if (bridgeEnabled_ && bridge_) {
        bridge_->selectFleetVehicle(carId);
    }
}

void CarlaControlManager::setDestination(const double x, const double y)
{
    controller_->setDestination(x, y);
    refreshDestinationHint();
    const double dx = x - sim_->x;
    const double dy = y - sim_->y;
    sim_->navRemainM = qSqrt(dx * dx + dy * dy);
    emit navGuidanceChanged(QStringLiteral("↑"), QStringLiteral("%1 m").arg(qRound(sim_->navRemainM)));
}

void CarlaControlManager::tickSim()
{
    if (!simControl_ || !controller_) {
        return;
    }

    const double dt = 0.05;
    if (controller_->emergencyStop()) {
        sim_->speedMps = 0.0;
    } else if (controller_->autopilot()) {
        sim_->speedMps = qMin(14.0, sim_->speedMps + 0.8 * dt);
        sim_->yaw += 0.02 * dt;
    } else {
        const double accel = controller_->throttle() * 4.0 - controller_->brake() * 6.0;
        sim_->speedMps = qBound(0.0, sim_->speedMps + accel * dt, 22.0);
        if (controller_->reverse()) {
            sim_->speedMps = -qMin(6.0, qAbs(sim_->speedMps));
        }
        sim_->yaw += controller_->steer() * 0.9 * dt * (sim_->speedMps >= 0 ? 1.0 : -1.0);
    }

    const double dir = sim_->speedMps >= 0 ? 1.0 : -1.0;
    sim_->x += dir * qAbs(sim_->speedMps) * qCos(sim_->yaw) * dt;
    sim_->y += dir * qAbs(sim_->speedMps) * qSin(sim_->yaw) * dt;

    if (controller_->hasDestination()) {
        const double dx = controller_->destinationX() - sim_->x;
        const double dy = controller_->destinationY() - sim_->y;
        sim_->navRemainM = qSqrt(dx * dx + dy * dy);
        const double bearing = qAtan2(dy, dx);
        const double delta = qAtan2(qSin(bearing - sim_->yaw), qCos(bearing - sim_->yaw));
        QString arrow = QStringLiteral("↑");
        if (delta > 0.45) {
            arrow = QStringLiteral("→");
        } else if (delta < -0.45) {
            arrow = QStringLiteral("←");
        }
        emit navGuidanceChanged(arrow, QStringLiteral("%1 m").arg(qRound(sim_->navRemainM)));
    }

    if (simAdapter_) {
        simAdapter_->applyExternalState(
            sim_->x, sim_->y, sim_->z, sim_->yaw, sim_->speedMps, controller_->steer() * 35.0,
            controller_->throttle(), controller_->brake(),
            controller_->reverse() ? 2 : (sim_->speedMps > 0.5 ? 3 : 1));
    }
}

void CarlaControlManager::onControllerChanged()
{
    if (hud_) {
        hud_->updateControlTelemetry(
            controller_->throttle(), controller_->brake(), controller_->steer(),
            controller_->autopilot(), controller_->emergencyStop(), controller_->reverse());
    }
    if (controller_->autopilot()) {
        consoleMode_ = ConsoleMode::Autonomous;
    } else if (simControl_) {
        consoleMode_ = ConsoleMode::Simulation;
    } else if (bridgeEnabled_) {
        consoleMode_ = ConsoleMode::Manual;
    }
    updateDriveModeBadge();
}

void CarlaControlManager::updateDriveModeBadge()
{
    QString label;
    switch (consoleMode_) {
    case ConsoleMode::Simulation:
        label = QStringLiteral("SIMULATION");
        break;
    case ConsoleMode::CarlaLive:
        label = controller_->autopilot() ? QStringLiteral("AUTONOMOUS") : QStringLiteral("CARLA LIVE");
        break;
    case ConsoleMode::Autonomous:
        label = QStringLiteral("AUTONOMOUS");
        break;
    case ConsoleMode::Manual:
        label = QStringLiteral("MANUAL");
        break;
    default:
        label = QStringLiteral("STANDBY");
        break;
    }
    if (controller_->emergencyStop()) {
        label = QStringLiteral("E-STOP");
    }
    if (hud_) {
        hud_->setDriveModeBadge(label);
    }
    emit driveModeChanged(label);
}

}  // namespace sdv::dash::carla::control
