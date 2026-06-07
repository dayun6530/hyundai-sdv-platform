#pragma once

#include <QWidget>

namespace sdv::dash::ui {

/** Bottom cockpit chrome: steering wheel with HYUNDAI center cap (1st-person). */
class SteeringWheelOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SteeringWheelOverlay(QWidget* parent = nullptr);

    void setChromeVisible(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawButtonCluster(QPainter& painter, const QRectF& area, bool leftSide) const;

    bool chromeVisible_{true};
};

}  // namespace sdv::dash::ui
