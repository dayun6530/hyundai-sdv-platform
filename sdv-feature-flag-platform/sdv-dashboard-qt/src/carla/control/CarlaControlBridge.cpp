#include "carla/control/CarlaControlBridge.h"

#include "carla/control/CarlaVehicleController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#if defined(SDV_HAVE_QT_WEBSOCKETS)
#include <QWebSocket>
#endif

namespace sdv::dash::carla::control {

class CarlaControlBridge::Private {
public:
#if defined(SDV_HAVE_QT_WEBSOCKETS)
    QWebSocket socket;
#endif
    QTimer tick;
};

CarlaControlBridge::CarlaControlBridge(QObject* parent)
    : QObject(parent), d_(new Private)
{
    d_->tick.setInterval(50);
    connect(&d_->tick, &QTimer::timeout, this, &CarlaControlBridge::onTick);

#if defined(SDV_HAVE_QT_WEBSOCKETS)
    connect(&d_->socket, &QWebSocket::connected, this, &CarlaControlBridge::onConnected);
    connect(&d_->socket, &QWebSocket::disconnected, this, &CarlaControlBridge::onDisconnected);
    connect(&d_->socket, &QWebSocket::errorOccurred, this, &CarlaControlBridge::onError);
    connect(&d_->socket, &QWebSocket::textMessageReceived, this, [this](const QString& msg) {
        const QJsonObject root = QJsonDocument::fromJson(msg.toUtf8()).object();
        if (root.value(QStringLiteral("type")).toString() != QStringLiteral("carla_status")) {
            return;
        }
        const QJsonObject nav = root.value(QStringLiteral("navigation")).toObject();
        const QString arrow = nav.value(QStringLiteral("arrow")).toString(root.value(QStringLiteral("nav_arrow")).toString());
        const double remain = nav.value(QStringLiteral("remain_m")).toDouble(root.value(QStringLiteral("nav_remain_m")).toDouble());
        if (!arrow.isEmpty()) {
            emit navigationUpdate(arrow, QStringLiteral("%1 m").arg(qRound(remain)));
        }
        const QJsonObject autonomy = root.value(QStringLiteral("autonomy")).toObject();
        const QString mode = autonomy.value(QStringLiteral("mode")).toString();
        const QString state = autonomy.value(QStringLiteral("state")).toString();
        const QString maneuver = autonomy.value(QStringLiteral("maneuver")).toString();
        QString detail;
        if (maneuver == QLatin1String("agents_unavailable")) {
            detail = QStringLiteral("Install CARLA agents (verify_carla_agents.py)");
        } else if (maneuver == QLatin1String("grp_unavailable")) {
            detail = QStringLiteral("GlobalRoutePlanner unavailable");
        }
        if (!mode.isEmpty() || !state.isEmpty()) {
            emit agentStatusUpdate(mode, state, maneuver, detail);
        }
    });
#endif
}

CarlaControlBridge::~CarlaControlBridge()
{
    disconnectBridge();
    delete d_;
}

void CarlaControlBridge::setController(CarlaVehicleController* controller)
{
    if (controller_ == controller) {
        return;
    }
    if (controller_) {
        disconnect(controller_, &CarlaVehicleController::stateChanged, this, &CarlaControlBridge::flushCommand);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &CarlaVehicleController::stateChanged, this, &CarlaControlBridge::flushCommand);
    }
}

void CarlaControlBridge::setWebSocketUrl(const QUrl& url)
{
    wsUrl_ = url;
}

void CarlaControlBridge::setEnabled(const bool enabled)
{
    enabled_ = enabled;
    if (enabled) {
        connectBridge();
    } else {
        disconnectBridge();
    }
}

void CarlaControlBridge::connectBridge()
{
#if !defined(SDV_HAVE_QT_WEBSOCKETS)
    emit statusMessage(QStringLiteral("Control bridge requires Qt WebSockets"));
    return;
#else
    if (!enabled_) {
        return;
    }
    if (d_->socket.state() == QAbstractSocket::ConnectedState) {
        return;
    }
    emit statusMessage(QStringLiteral("Connecting control bridge %1").arg(wsUrl_.toString()));
    d_->socket.open(wsUrl_);
#endif
}

void CarlaControlBridge::disconnectBridge()
{
    d_->tick.stop();
#if defined(SDV_HAVE_QT_WEBSOCKETS)
    if (d_->socket.state() != QAbstractSocket::UnconnectedState) {
        d_->socket.close();
    }
#endif
    connected_ = false;
    emit connectionChanged(false);
}

void CarlaControlBridge::flushCommand()
{
    if (connected_) {
        onTick();
    }
}

void CarlaControlBridge::onConnected()
{
    connected_ = true;
    d_->tick.start();
    emit connectionChanged(true);
    emit statusMessage(QStringLiteral("CARLA control bridge connected"));
    onTick();
}

void CarlaControlBridge::onDisconnected()
{
    connected_ = false;
    d_->tick.stop();
    emit connectionChanged(false);
    emit statusMessage(QStringLiteral("CARLA control bridge disconnected"));
}

void CarlaControlBridge::onError()
{
#if defined(SDV_HAVE_QT_WEBSOCKETS)
    emit statusMessage(QStringLiteral("Control WS: %1").arg(d_->socket.errorString()));
#endif
}

void CarlaControlBridge::onTick()
{
#if !defined(SDV_HAVE_QT_WEBSOCKETS)
    return;
#else
    if (!controller_ || d_->socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    const QJsonDocument doc(controller_->toCommandJson());
    d_->socket.sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    if (!controller_->autonomousAction().isEmpty()) {
        controller_->clearAutonomousAction();
    }
#endif
}

}  // namespace sdv::dash::carla::control
