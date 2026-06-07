#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QWebSocket;

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
class QWebSocketServer;
#endif

namespace sdv::dash::carla::integration {

/**
 * WebSocket publisher for Phase-1 telemetry (port 9090 by default).
 * Allows CarlaCameraReceiver and RosBridgeClient to consume the same CARLA stream.
 */
class CarlaWsPublisher : public QObject {
    Q_OBJECT
public:
    explicit CarlaWsPublisher(QObject* parent = nullptr);
    ~CarlaWsPublisher() override;

    bool start(quint16 port = 9090);
    void stop();
    bool isListening() const;

    void publishFrame(const QByteArray& utf8Json);
    void publishFrames(const QVector<QByteArray>& frames);

signals:
    void clientCountChanged(int count);
    void statusMessage(const QString& line);

private:
    void onNewConnection();
    void sendHello(QWebSocket* client);

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    QWebSocketServer* server_{nullptr};
    QVector<QWebSocket*> clients_;
#endif
    quint16 port_{9090};
    bool listening_{false};
};

}  // namespace sdv::dash::carla::integration
