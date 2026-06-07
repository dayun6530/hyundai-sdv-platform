#pragma once

#include "twin/TwinTypes.h"

#include <QString>

namespace sdv::dash::ops {
struct EgoPose;
struct PerceptionObjectList;
struct Trajectory;
}

namespace sdv::dash::twin {

/** Phase E — swap CARLA / Autoware without changing dashboard widgets. */
class ITwinLocalizationAdapter {
public:
    virtual ~ITwinLocalizationAdapter() = default;
    virtual bool isValid() const = 0;
    virtual sdv::dash::ops::EgoPose egoPose() const = 0;
    virtual double speedLimitKmh() const = 0;
};

class ITwinPerceptionAdapter {
public:
    virtual ~ITwinPerceptionAdapter() = default;
    virtual sdv::dash::ops::PerceptionObjectList perception() const = 0;
    virtual QVector<TwinTrafficLight> trafficLights() const = 0;
};

class ITwinPlanningAdapter {
public:
    virtual ~ITwinPlanningAdapter() = default;
    virtual sdv::dash::ops::Trajectory plannedRoute() const = 0;
    virtual TwinNavigationState navigation() const = 0;
};

class ITwinControlAdapter {
public:
    virtual ~ITwinControlAdapter() = default;
    virtual void setDestination(double x, double y) = 0;
    virtual void startAutonomous(const QString& mode) = 0;
    virtual void pauseAutonomous() = 0;
    virtual void resumeAutonomous() = 0;
    virtual void emergencyStop() = 0;
};

}  // namespace sdv::dash::twin
