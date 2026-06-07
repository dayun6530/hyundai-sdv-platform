#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QGraphicsOpacityEffect;

namespace sdv::dash::ui {

/** Lightweight UI motion helpers (no telemetry coupling). */
class UiAnimation {
public:
    static QGraphicsOpacityEffect* attachPulse(QWidget* target, int periodMs = 1400);
    static void animateProgress(QProgressBar* bar, int targetValue, int durationMs = 320);
    static void animateLabelNumber(QLabel* label, const QString& text);
    static void fadeIn(QWidget* widget, int durationMs = 420);
};

}  // namespace sdv::dash::ui
