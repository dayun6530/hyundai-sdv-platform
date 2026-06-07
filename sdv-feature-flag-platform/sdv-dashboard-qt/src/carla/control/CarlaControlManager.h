#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "carla/control/CarlaVehicleController.h"

class QTimer;
class QWidget;

namespace sdv::dash::carla {
class CarlaCockpitView;
class HudOverlayWidget;
}

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla::integration {
class CarlaSimAdapter;
}

namespace sdv::dash::carla::control {

class CarlaControlBridge;
class CarlaControlPanel;

/** Orchestrates keyboard/UI control → bridge or sim adapter. */
class CarlaControlManager : public QObject {
    Q_OBJECT
public:
    enum class ConsoleMode {
        Off,
        Simulation,
        CarlaLive,
        Autonomous,
        Manual,
    };

    explicit CarlaControlManager(QObject* parent = nullptr);
    ~CarlaControlManager() override;

    CarlaVehicleController* vehicleController() const { return controller_; }
    CarlaControlBridge* controlBridge() const { return bridge_; }
    ConsoleMode consoleMode() const { return consoleMode_; }

    void configure(const QString& carlaModeCli,
                   sdv::dash::carla::CarlaCockpitView* cockpit,
                   sdv::dash::carla::HudOverlayWidget* hud,
                   sdv::dash::carla::integration::CarlaSimAdapter* simAdapter = nullptr,
                   const QUrl& controlWsUrl = QUrl(QStringLiteral("ws://127.0.0.1:9093")),
                   sdv::dash::ops::OpsTelemetryPipeline* pipeline = nullptr);

    void setCameraSlot(CarlaVehicleController::CameraSlot slot);
    void toggleAutopilot();
    void triggerEmergencyStop();
    void setDestination(double x, double y);
    bool startAutonomousDriving(const QString& mode);
    void pauseAutonomousDriving();
    void resumeAutonomousDriving();
    void selectFleetVehicle(const QString& carId);

    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void driveModeChanged(const QString& badgeLabel);
    void statusMessage(const QString& line);
    void navGuidanceChanged(const QString& arrows, const QString& distanceText);
    void agentStatusChanged(const QString& agentMode, const QString& runState, const QString& maneuver);
    void destinationHintChanged(const QString& hint);

public slots:
    void tickSim();

private:
    void updateDriveModeBadge();
    void attachCockpit(sdv::dash::carla::CarlaCockpitView* cockpit);
    void refreshDestinationHint();
    void onControllerChanged();

    CarlaVehicleController* controller_{nullptr};
    CarlaControlBridge* bridge_{nullptr};
    CarlaControlPanel* panel_{nullptr};
    sdv::dash::carla::CarlaCockpitView* cockpit_{nullptr};
    sdv::dash::carla::HudOverlayWidget* hud_{nullptr};
    sdv::dash::carla::integration::CarlaSimAdapter* simAdapter_{nullptr};
    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};

    ConsoleMode consoleMode_{ConsoleMode::Off};
    bool bridgeEnabled_{false};
    bool simControl_{false};

    class SimState;
    SimState* sim_{nullptr};

    QTimer* inputTimer_{nullptr};
    QTimer* simTimer_{nullptr};
    bool appDriveFilterInstalled_{false};

    static bool isDriveKey(int key);
    static bool focusBlocksDriveKeys();
    bool handleDriveKeyEvent(QEvent* event);
};

}  // namespace sdv::dash::carla::control
