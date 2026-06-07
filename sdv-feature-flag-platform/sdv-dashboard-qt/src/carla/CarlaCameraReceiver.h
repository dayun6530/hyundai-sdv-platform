#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>

namespace sdv::dash::carla {

/**
 * Receives CARLA front-camera frames without modifying RosBridgeClient / SignalManager.
 * Listens on a dedicated WebSocket for `front_camera` JSON envelopes (base64 JPEG),
 * or polls an optional HTTP MJPEG endpoint (CARLA_CAMERA_HTTP).
 */
class CarlaCameraReceiver : public QObject {
    Q_OBJECT
public:
    explicit CarlaCameraReceiver(QObject* parent = nullptr);
    ~CarlaCameraReceiver() override;

    bool isConnected() const { return connected_; }
    bool hasLiveFrame() const { return hasLiveFrame_; }

public slots:
    void start(const QUrl& webSocketUrl);
    void stop();
    /** Direct frame from CarlaDataBridge (bypasses WebSocket). */
    void ingestFrame(const QImage& frame);
    /** Parse a `front_camera` / pygame_map JSON envelope (shared with RosBridgeClient). */
    bool ingestJsonEnvelope(const QByteArray& utf8Frame);
    void showWaitingPlaceholder();

signals:
    void frameReady(const QImage& frame);
    void connectionChanged(bool connected);
    void statusMessage(const QString& message);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError();
    void onTextMessageReceived(const QString& message);
    void pollHttpFrame();
    void showPlaceholder();

private:
    bool decodeCameraEnvelope(const QByteArray& utf8Frame);
    void deliverFrame(const QImage& image);
    QImage buildPlaceholder(const QString& caption) const;

    class Private;
    Private* d_{nullptr};
    bool connected_{false};
    bool hasLiveFrame_{false};
    bool stopRequested_{true};
};

}  // namespace sdv::dash::carla
