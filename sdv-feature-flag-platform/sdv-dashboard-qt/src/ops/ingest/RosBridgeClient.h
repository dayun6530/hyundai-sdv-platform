#pragma once

#include <QObject>
#include <QUrl>

namespace sdv::dash::ops {

/// WebSocket client for sdv_ros_bridge telemetry (runs on a dedicated QThread).
class RosBridgeClient : public QObject {
    Q_OBJECT
public:
    explicit RosBridgeClient(QObject* parent = nullptr);
    ~RosBridgeClient() override;

    bool isConnected() const { return connected_; }

public slots:
    void start(const QUrl& url);
    void stop();

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& message);
    void jsonFrameReceived(const QByteArray& utf8Frame);
    /** Large `front_camera` / pygame_map envelopes (not passed to SignalManager). */
    void cameraJsonReceived(const QByteArray& utf8Frame);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError();
    void onTextMessageReceived(const QString& message);
    void attemptReconnect();

private:
    void ensureInitialized();
    void openSocket();
    void scheduleReconnect();
    void resetBackoff();

    class Private;
    Private* d_{nullptr};
    bool connected_{false};
    bool stopRequested_{true};
};

}  // namespace sdv::dash::ops
