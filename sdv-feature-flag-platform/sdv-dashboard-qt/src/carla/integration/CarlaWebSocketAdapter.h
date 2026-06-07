#pragma once

#include "carla/integration/CarlaApiAdapter.h"

#include <QUrl>

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
class QWebSocket;
#endif

namespace sdv::dash::carla::integration {

/** Receives `carla_snapshot` JSON from carla_integration_bridge.py (port 9092). */
class CarlaWebSocketAdapter : public CarlaApiAdapter {
    Q_OBJECT
public:
    explicit CarlaWebSocketAdapter(QObject* parent = nullptr);
    ~CarlaWebSocketAdapter() override;

    void setEndpoint(const QUrl& url);
    void start() override;
    void stop() override;
    bool isRunning() const override;
    QString statusLine() const override;

private:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);

    QUrl endpoint_{QStringLiteral("ws://127.0.0.1:9092")};
    bool connected_{false};
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    QWebSocket* socket_{nullptr};
#endif
};

}  // namespace sdv::dash::carla::integration
