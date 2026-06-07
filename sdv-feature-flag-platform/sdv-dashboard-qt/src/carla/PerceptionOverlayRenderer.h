#pragma once

#include <QWidget>

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla {

/** Bounding boxes for perception objects over the CARLA feed. */
class PerceptionOverlayRenderer : public QWidget {
    Q_OBJECT
public:
    explicit PerceptionOverlayRenderer(QWidget* parent = nullptr);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onTelemetryChanged();

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
};

}  // namespace sdv::dash::carla
