#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>

#include "carla/integration/CarlaTypes.h"
#include "carla/integration/HudViewModel.h"

namespace sdv::dash::carla::integration {

class CarlaApiAdapter;
class CarlaSimAdapter;
class CarlaWsPublisher;

/**
 * CARLA → extractors → Phase-1 telemetry + HUD + optional WS publish.
 * Feeds OpsTelemetryPipeline via SignalManager::ingestJsonFrame (no pipeline changes).
 */
class CarlaDataBridge : public QObject {
    Q_OBJECT
public:
    enum class AdapterKind { Simulator, WebSocket };

    explicit CarlaDataBridge(QObject* parent = nullptr);
    ~CarlaDataBridge() override;

    HudViewModel* hudViewModel() const { return hudViewModel_; }

    void setAdapterKind(AdapterKind kind);
    void setCarlaWebSocketUrl(const QUrl& url);
    void setPublishPort(quint16 port);
    void setPublishToWebSocket(bool enabled);
    void setIngestToPipeline(bool enabled);

    void start();
    void stop();
    bool isRunning() const;
    CarlaSimAdapter* simulatorAdapter() const;

signals:
    void telemetryFrameReady(const QByteArray& utf8Json);
    void cameraFrameReady(const QImage& image);
    void twinSnapshotReady(const CarlaWorldSnapshot& snapshot);
    void statusMessage(const QString& line);

private:
    void onSnapshot(const CarlaWorldSnapshot& snapshot);

    CarlaApiAdapter* adapter_{nullptr};
    AdapterKind adapterKind_{AdapterKind::Simulator};
    HudViewModel* hudViewModel_{nullptr};
    CarlaWsPublisher* publisher_{nullptr};
    QUrl wsAdapterUrl_{QStringLiteral("ws://127.0.0.1:9092")};
    quint16 publishPort_{9090};
    bool publishWs_{true};
    bool ingestPipeline_{true};
    bool helloSent_{false};
};

}  // namespace sdv::dash::carla::integration
