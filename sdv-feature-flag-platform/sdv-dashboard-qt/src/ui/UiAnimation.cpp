#include "ui/UiAnimation.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>

namespace sdv::dash::ui {

QGraphicsOpacityEffect* UiAnimation::attachPulse(QWidget* target, const int periodMs)
{
    if (!target) {
        return nullptr;
    }
    auto* effect = new QGraphicsOpacityEffect(target);
    effect->setOpacity(1.0);
    target->setGraphicsEffect(effect);

    auto* anim = new QPropertyAnimation(effect, "opacity", target);
    anim->setDuration(periodMs);
    anim->setStartValue(0.45);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    anim->setLoopCount(-1);

    auto* group = new QSequentialAnimationGroup(target);
    group->addAnimation(anim);
    auto* reverse = new QPropertyAnimation(effect, "opacity", target);
    reverse->setDuration(periodMs);
    reverse->setStartValue(1.0);
    reverse->setEndValue(0.45);
    reverse->setEasingCurve(QEasingCurve::InOutSine);
    group->addAnimation(reverse);
    group->start();

    return effect;
}

void UiAnimation::animateProgress(QProgressBar* bar, const int targetValue, const int durationMs)
{
    if (!bar) {
        return;
    }
    auto* anim = new QPropertyAnimation(bar, "value", bar);
    anim->setDuration(durationMs);
    anim->setStartValue(bar->value());
    anim->setEndValue(qBound(bar->minimum(), targetValue, bar->maximum()));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UiAnimation::animateLabelNumber(QLabel* label, const QString& text)
{
    if (!label) {
        return;
    }
    label->setText(text);
    label->setStyleSheet(label->styleSheet() + QStringLiteral(" color: #f0f9ff;"));
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(label->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(label);
        label->setGraphicsEffect(effect);
    }
    auto* anim = new QPropertyAnimation(effect, "opacity", label);
    anim->setDuration(180);
    anim->setStartValue(0.55);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void UiAnimation::fadeIn(QWidget* widget, const int durationMs)
{
    if (!widget) {
        return;
    }
    auto* effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(0.0);
    widget->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", widget);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace sdv::dash::ui
