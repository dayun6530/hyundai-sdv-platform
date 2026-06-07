#pragma once

#include "carla/integration/CarlaTypes.h"
#include "twin/TwinTypes.h"

#include <QObject>

namespace sdv::dash::carla::control {
class CarlaControlManager;
}

namespace sdv::dash::carla::integration {
struct CarlaWorldSnapshot;
}

namespace sdv::dash::twin {

/** CARLA-backed twin facade; AutowareTwinAdapters mirrors this API later. */
class CarlaTwinAdapters : public QObject {
    Q_OBJECT
public:
    explicit CarlaTwinAdapters(QObject* parent = nullptr);

    void setControlManager(sdv::dash::carla::control::CarlaControlManager* manager);
    void ingestSnapshot(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap, const TwinWorldState& twin);

    const TwinWorldState& worldState() const { return world_; }
    const sdv::dash::carla::integration::CarlaWorldSnapshot* lastSnapshot() const { return hasSnap_ ? &lastSnap_ : nullptr; }

    void setDestination(double x, double y);
    void startAutonomous(const QString& mode);
    void pauseAutonomous();
    void resumeAutonomous();
    void emergencyStop();

signals:
    void worldStateChanged(const TwinWorldState& state);

private:
    TwinWorldState world_;
    sdv::dash::carla::integration::CarlaWorldSnapshot lastSnap_;
    bool hasSnap_{false};
    sdv::dash::carla::control::CarlaControlManager* controlManager_{nullptr};
};

}  // namespace sdv::dash::twin
