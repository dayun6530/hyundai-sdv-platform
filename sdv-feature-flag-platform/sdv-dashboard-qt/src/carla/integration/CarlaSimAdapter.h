#pragma once

#include "carla/integration/CarlaApiAdapter.h"

#include <QTimer>

namespace sdv::dash::carla::integration {

/** In-process CARLA demo simulator when the simulator is offline. */
class CarlaSimAdapter : public CarlaApiAdapter {
    Q_OBJECT
public:
    explicit CarlaSimAdapter(QObject* parent = nullptr);

    void start() override;
    void stop() override;
    bool isRunning() const override { return running_; }
    QString statusLine() const override;

    /** Driven by CarlaControlManager in simulation mode. */
    void applyExternalState(double x, double y, double z, double yawRad, double speedMps, double steerDeg,
                            double throttle, double brake, int gear);

private:
    void tick();

    QTimer timer_;
    bool running_{false};
    double phase_{0.0};
    bool useExternal_{false};
    double extX_{100.0};
    double extY_{200.0};
    double extZ_{0.5};
    double extYaw_{0.0};
    double extSpeed_{0.0};
    double extSteerDeg_{0.0};
    double extThrottle_{0.0};
    double extBrake_{0.0};
    int extGear_{3};
};

}  // namespace sdv::dash::carla::integration
