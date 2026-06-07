#include "ops/ingest/RosBridgeClient.h"

#include <QTimer>
#include <QWebSocket>

namespace sdv::dash::ops {

namespace {

constexpr int kInitialBackoffMs = 1000;
constexpr int kMaxBackoffMs = 30000;

}  // namespace

class RosBridgeClient::Private {
public:
    QUrl url;
    QWebSocket* socket{nullptr};
    QTimer* reconnectTimer{nullptr};
    int backoffMs{kInitialBackoffMs};
};

RosBridgeClient::RosBridgeClient(QObject* parent) : QObject(parent), d_(new Private) {}

RosBridgeClient::~RosBridgeClient()
{
    stop();
    delete d_->socket;
    delete d_->reconnectTimer;
    delete d_;
    d_ = nullptr;
}

void RosBridgeClient::ensureInitialized()
{
    if (d_->socket) {
        return;
    }

    d_->socket = new QWebSocket();
    d_->reconnectTimer = new QTimer();
    d_->reconnectTimer->setSingleShot(true);

    connect(d_->reconnectTimer, &QTimer::timeout, this, &RosBridgeClient::attemptReconnect);
    connect(d_->socket, &QWebSocket::connected, this, &RosBridgeClient::onSocketConnected);
    connect(d_->socket, &QWebSocket::disconnected, this, &RosBridgeClient::onSocketDisconnected);
    connect(d_->socket, &QWebSocket::errorOccurred, this, &RosBridgeClient::onSocketError);
    connect(d_->socket, &QWebSocket::textMessageReceived, this, &RosBridgeClient::onTextMessageReceived);
}

void RosBridgeClient::start(const QUrl& url)
{
    ensureInitialized();
    stopRequested_ = false;
    d_->url = url;
    resetBackoff();
    openSocket();
}

void RosBridgeClient::stop()
{
    stopRequested_ = true;
    if (!d_->reconnectTimer || !d_->socket) {
        connected_ = false;
        return;
    }
    d_->reconnectTimer->stop();
    if (d_->socket->state() != QAbstractSocket::UnconnectedState) {
        d_->socket->close();
    }
    connected_ = false;
}

void RosBridgeClient::openSocket()
{
    if (!d_->socket || stopRequested_ || !d_->url.isValid()) {
        return;
    }

    if (d_->socket->state() == QAbstractSocket::ConnectedState ||
        d_->socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    d_->socket->open(d_->url);
}

void RosBridgeClient::onSocketConnected()
{
    connected_ = true;
    resetBackoff();
    emit connected();
}

void RosBridgeClient::onSocketDisconnected()
{
    const bool wasConnected = connected_;
    connected_ = false;
    if (wasConnected) {
        emit disconnected();
    }
    if (!stopRequested_) {
        scheduleReconnect();
    }
}

void RosBridgeClient::onSocketError()
{
    if (!d_->socket) {
        return;
    }
    emit connectionError(d_->socket->errorString());
    if (!stopRequested_) {
        scheduleReconnect();
    }
}

void RosBridgeClient::onTextMessageReceived(const QString& message)
{
    const QByteArray raw = message.toUtf8();
    // JPEG camera frames flood the WS; keep them off the telemetry JSON path.
    if (raw.contains("\"front_camera\"") || raw.contains("\"carla_camera\"") ||
        raw.contains("\"pygame_map\"")) {
        emit cameraJsonReceived(raw);
        return;
    }
    emit jsonFrameReceived(raw);
}

void RosBridgeClient::attemptReconnect()
{
    openSocket();
}

void RosBridgeClient::scheduleReconnect()
{
    if (stopRequested_ || !d_->reconnectTimer) {
        return;
    }
    d_->reconnectTimer->start(d_->backoffMs);
    d_->backoffMs = qMin(d_->backoffMs * 2, kMaxBackoffMs);
}

void RosBridgeClient::resetBackoff()
{
    d_->backoffMs = kInitialBackoffMs;
}

}  // namespace sdv::dash::ops
