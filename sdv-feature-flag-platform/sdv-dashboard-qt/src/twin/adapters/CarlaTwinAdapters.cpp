#include "twin/adapters/CarlaTwinAdapters.h"

#include "carla/control/CarlaControlManager.h"
#include "carla/integration/CarlaTypes.h"

namespace sdv::dash::twin {

CarlaTwinAdapters::CarlaTwinAdapters(QObject* parent) : QObject(parent) {}

void CarlaTwinAdapters::setControlManager(sdv::dash::carla::control::CarlaControlManager* manager)
{
    controlManager_ = manager;
}

void CarlaTwinAdapters::ingestSnapshot(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap,
                                       const TwinWorldState& twin)
{
    lastSnap_ = snap;
    hasSnap_ = true;
    world_ = twin;
    world_.valid = true;
    emit worldStateChanged(world_);
}

void CarlaTwinAdapters::setDestination(const double x, const double y)
{
    if (controlManager_) {
        controlManager_->setDestination(x, y);
    }
}

void CarlaTwinAdapters::startAutonomous(const QString& mode)
{
    if (controlManager_) {
        controlManager_->startAutonomousDriving(mode);
    }
}

void CarlaTwinAdapters::pauseAutonomous()
{
    if (controlManager_) {
        controlManager_->pauseAutonomousDriving();
    }
}

void CarlaTwinAdapters::resumeAutonomous()
{
    if (controlManager_) {
        controlManager_->resumeAutonomousDriving();
    }
}

void CarlaTwinAdapters::emergencyStop()
{
    if (controlManager_) {
        controlManager_->triggerEmergencyStop();
    }
}

}  // namespace sdv::dash::twin
