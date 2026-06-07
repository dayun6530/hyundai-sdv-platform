#include "carla/integration/CarlaWsPublisher.h"

#include "carla/integration/CarlaTelemetrySerializer.h"

#include <QHostAddress>

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
#include <QWebSocket>
#include <QWebSocketServer>
#endif

namespace sdv::dash::carla::integration {

CarlaWsPublisher::CarlaWsPublisher(QObject* parent) : QObject(parent) {}

CarlaWsPublisher::~CarlaWsPublisher()
{
    stop();
}

bool CarlaWsPublisher::start(const quint16 port)
{
#if !defined(SDV_HAVE_QT_WEBSOCKETS) || !SDV_HAVE_QT_WEBSOCKETS
    Q_UNUSED(port);
    emit statusMessage(QStringLiteral("CARLA WS publisher unavailable (Qt WebSockets missing)"));
    return false;
#else
    stop();
    port_ = port;
    server_ = new QWebSocketServer(
        QStringLiteral("CarlaIntegration"),
        QWebSocketServer::NonSecureMode,
        this);
    if (!server_->listen(QHostAddress::LocalHost, port_)) {
        emit statusMessage(QStringLiteral("CARLA WS publisher failed: %1").arg(server_->errorString()));
        delete server_;
        server_ = nullptr;
        return false;
    }
    connect(server_, &QWebSocketServer::newConnection, this, &CarlaWsPublisher::onNewConnection);
    listening_ = true;
    emit statusMessage(QStringLiteral("CARLA WS publisher on ws://127.0.0.1:%1").arg(port_));
    return true;
#endif
}

void CarlaWsPublisher::stop()
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    for (QWebSocket* client : clients_) {
        client->close();
        client->deleteLater();
    }
    clients_.clear();
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
#endif
    listening_ = false;
    emit clientCountChanged(0);
}

bool CarlaWsPublisher::isListening() const
{
    return listening_;
}

void CarlaWsPublisher::publishFrame(const QByteArray& utf8Json)
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    for (QWebSocket* client : clients_) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->sendTextMessage(QString::fromUtf8(utf8Json));
        }
    }
#else
    Q_UNUSED(utf8Json);
#endif
}

void CarlaWsPublisher::publishFrames(const QVector<QByteArray>& frames)
{
    for (const QByteArray& frame : frames) {
        publishFrame(frame);
    }
}

void CarlaWsPublisher::onNewConnection()
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    if (!server_) {
        return;
    }
    QWebSocket* client = server_->nextPendingConnection();
    if (!client) {
        return;
    }
    clients_.append(client);
    connect(client, &QWebSocket::disconnected, this, [this, client]() {
        clients_.removeAll(client);
        client->deleteLater();
        emit clientCountChanged(clients_.size());
    });
    sendHello(client);
    emit clientCountChanged(clients_.size());
#endif
}

void CarlaWsPublisher::sendHello(QWebSocket* client)
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    if (client) {
        client->sendTextMessage(QString::fromUtf8(CarlaTelemetrySerializer::bridgeHello()));
    }
#else
    Q_UNUSED(client);
#endif
}

}  // namespace sdv::dash::carla::integration
