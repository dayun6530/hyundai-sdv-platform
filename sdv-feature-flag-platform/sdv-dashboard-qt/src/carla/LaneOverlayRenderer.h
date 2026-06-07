#pragma once

#include <QWidget>

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla {

/** Green route / lane guidance painted over the CARLA camera feed. */
class LaneOverlayRenderer : public QWidget {
    Q_OBJECT
public:
    explicit LaneOverlayRenderer(QWidget* parent = nullptr);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onTelemetryChanged();

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
};

}  // namespace sdv::dash::carla
