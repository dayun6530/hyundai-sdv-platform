#pragma once

#include <QString>
#include <QVector>

namespace sdv::dash::twin {

struct TwinTrafficLight {
    QString id;
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double yawRad{0.0};
    /** red | yellow | green | unknown */
    QString state;
};

struct TwinNavigationState {
    QString maneuverArrow{QStringLiteral("↑")};
    QString maneuverText;
    double distanceToGoalM{0.0};
    double etaSec{0.0};
    double routeProgress{0.0};
};

struct TwinAutonomyState {
    QString agentMode;   /** off | basic | behavior */
    QString runState;    /** idle | driving | paused */
    QString maneuver;
};

struct TwinWorldState {
    qint64 stampNs{0};
    double speedLimitKmh{0.0};
    QVector<TwinTrafficLight> trafficLights;
    TwinNavigationState navigation;
    TwinAutonomyState autonomy;
    bool valid{false};
};

}  // namespace sdv::dash::twin
