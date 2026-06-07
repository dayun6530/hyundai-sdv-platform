#include "carla/CinematicOverlay.h"

#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QtMath>

namespace sdv::dash::carla {

CinematicOverlay::CinematicOverlay(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        phase_ += 0.02f;
        if (phase_ > 6.28f) {
            phase_ = 0.f;
        }
        update();
    });
    timer->start(48);
}

void CinematicOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();
    const int w = r.width();
    const int h = r.height();
    const float pulse = 0.5f + 0.5f * qSin(phase_);

    QRadialGradient vignette(r.center(), qMax(w, h) * 0.78);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.5, QColor(0, 0, 0, 20));
    vignette.setColorAt(0.82, QColor(0, 0, 0, 120));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 230));
    p.fillRect(r, vignette);

    QLinearGradient topBar(0, 0, 0, h * 0.22);
    topBar.setColorAt(0.0, QColor(2, 6, 18, 230));
    topBar.setColorAt(1.0, QColor(2, 6, 18, 0));
    p.fillRect(0, 0, w, static_cast<int>(h * 0.24), topBar);

    QLinearGradient bottomBar(0, h * 0.7, 0, h);
    bottomBar.setColorAt(0.0, QColor(2, 6, 18, 0));
    bottomBar.setColorAt(1.0, QColor(2, 6, 18, 240));
    p.fillRect(0, static_cast<int>(h * 0.66), w, h - static_cast<int>(h * 0.66), bottomBar);

    QLinearGradient hudGlow(0, h * 0.75, 0, h);
    hudGlow.setColorAt(0.0, QColor(34, 211, 238, 0));
    hudGlow.setColorAt(1.0, QColor(34, 211, 238, static_cast<int>(12 + 14 * pulse)));
    p.fillRect(0, static_cast<int>(h * 0.72), w, static_cast<int>(h * 0.28), hudGlow);

    QPen sideGlow(QColor(34, 211, 238, static_cast<int>(35 + 25 * pulse)), 2.5);
    p.setPen(sideGlow);
    p.drawLine(6, h / 6, 6, h * 5 / 6);
    p.drawLine(w - 6, h / 6, w - 6, h * 5 / 6);

    QLinearGradient horizon(0, h * 0.5, 0, h * 0.58);
    horizon.setColorAt(0.0, QColor(56, 189, 248, 0));
    horizon.setColorAt(0.5, QColor(56, 189, 248, static_cast<int>(22 + 14 * pulse)));
    horizon.setColorAt(1.0, QColor(56, 189, 248, 0));
    p.fillRect(0, static_cast<int>(h * 0.5), w, static_cast<int>(h * 0.08), horizon);
}

}  // namespace sdv::dash::carla
