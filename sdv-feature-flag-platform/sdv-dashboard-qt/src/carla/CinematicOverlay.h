#pragma once

#include <QWidget>

namespace sdv::dash::carla {

/** Vignette, letterbox gradients, and CES-style cockpit atmosphere. */
class CinematicOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CinematicOverlay(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float phase_{0.f};
};

}  // namespace sdv::dash::carla
