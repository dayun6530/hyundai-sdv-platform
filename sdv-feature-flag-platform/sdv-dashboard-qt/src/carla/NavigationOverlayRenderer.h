#pragma once

#include <QString>
#include <QWidget>

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla {

/** Turn guidance banner and navigation route corridor over the camera feed. */
class NavigationOverlayRenderer : public QWidget {
    Q_OBJECT
public:
    explicit NavigationOverlayRenderer(QWidget* parent = nullptr);

    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void setNavGuidance(const QString& maneuverArrows, const QString& distanceText);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onTelemetryChanged();

    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    QString navArrows_{QStringLiteral("↑")};
    QString navDistance_{QStringLiteral("—")};
    float navPulse_{0.f};
};

}  // namespace sdv::dash::carla
