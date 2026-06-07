#pragma once

#include <QObject>
#include <QUrl>

namespace sdv::dash::carla::control {

class CarlaVehicleController;

/** Sends control commands to CARLA Python bridge (ws://127.0.0.1:9093). */
class CarlaControlBridge : public QObject {
    Q_OBJECT
public:
    explicit CarlaControlBridge(QObject* parent = nullptr);
    ~CarlaControlBridge() override;

    void setController(CarlaVehicleController* controller);
    void setWebSocketUrl(const QUrl& url);
    void setEnabled(bool enabled);

    bool isConnected() const { return connected_; }

public slots:
    void connectBridge();
    void disconnectBridge();
    void flushCommand();
    void selectFleetVehicle(const QString& carId);

signals:
    void connectionChanged(bool connected);
    void statusMessage(const QString& line);
    void navigationUpdate(const QString& arrow, const QString& distanceText);
    void agentStatusUpdate(const QString& agentMode,
                           const QString& runState,
                           const QString& maneuver,
                           const QString& detail);

private slots:
    void onConnected();
    void onDisconnected();
    void onError();
    void onTick();

private:
    CarlaVehicleController* controller_{nullptr};
    QUrl wsUrl_{QStringLiteral("ws://127.0.0.1:9093")};
    bool enabled_{false};
    bool connected_{false};

    class Private;
    Private* d_{nullptr};
};

}  // namespace sdv::dash::carla::control
