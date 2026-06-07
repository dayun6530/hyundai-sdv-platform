#pragma once

#include <QObject>
#include <QString>

#include "carla/integration/CarlaTypes.h"

namespace sdv::dash::carla::integration {

/**
 * Abstract CARLA 0.9.15 data source.
 * Production: Python sidecar (carla_integration_bridge.py) or future libcarla client.
 */
class CarlaApiAdapter : public QObject {
    Q_OBJECT
public:
    explicit CarlaApiAdapter(QObject* parent = nullptr) : QObject(parent) {}
    ~CarlaApiAdapter() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual QString statusLine() const = 0;

signals:
    void snapshotReady(const sdv::dash::carla::integration::CarlaWorldSnapshot& snapshot);
    void statusMessage(const QString& line);
    void runningChanged(bool running);
};

}  // namespace sdv::dash::carla::integration

Q_DECLARE_METATYPE(sdv::dash::carla::integration::CarlaWorldSnapshot)
