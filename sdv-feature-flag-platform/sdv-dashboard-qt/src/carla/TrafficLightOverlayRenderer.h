#pragma once

#include "twin/TwinTypes.h"

#include <QVector>
#include <QWidget>

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla {

/** FSD-style traffic light indicators projected on the camera view. */
class TrafficLightOverlayRenderer : public QWidget {
    Q_OBJECT
public:
    explicit TrafficLightOverlayRenderer(QWidget* parent = nullptr);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void setTrafficLights(const QVector<sdv::dash::twin::TwinTrafficLight>& lights);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onPerceptionChanged();

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    QVector<sdv::dash::twin::TwinTrafficLight> lights_;
};

}  // namespace sdv::dash::carla
