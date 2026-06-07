#include "ui/OtaFlowDiagramWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QtMath>

namespace sdv::dash::ui {

namespace {

const char* kSteps[] = {"Download", "Verify", "Install", "Switch", "Validate", "Complete"};
constexpr int kStepCount = 6;

int stateToIndex(const QString& state)
{
    if (state == QLatin1String("Downloading")) {
        return 0;
    }
    if (state == QLatin1String("Verifying")) {
        return 1;
    }
    if (state == QLatin1String("Installing")) {
        return 2;
    }
    if (state == QLatin1String("Switching")) {
        return 3;
    }
    if (state == QLatin1String("Validating")) {
        return 4;
    }
    if (state == QLatin1String("Completed")) {
        return 5;
    }
    return -1;
}

}  // namespace

OtaFlowDiagramWidget::OtaFlowDiagramWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(88);
    setMaximumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        pulse_ += 0.08f;
        update();
    });
    timer->start(60);
}

void OtaFlowDiagramWidget::setDeploymentState(const QString& state)
{
    activeIndex_ = stateToIndex(state);
    failed_ = state == QLatin1String("Failed") || state == QLatin1String("Rollback");
    update();
}

void OtaFlowDiagramWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect().adjusted(4, 8, -4, -4);
    const int stepW = r.width() / kStepCount;
    const int cy = r.center().y();
    const float pulse = 0.5f + 0.5f * qSin(pulse_);

    p.setPen(QPen(QColor(51, 65, 85, 180), 2.0));
    p.drawLine(r.left() + stepW / 2, cy, r.right() - stepW / 2, cy);

    QFont labelFont = p.font();
    labelFont.setPixelSize(8);
    labelFont.setBold(true);
    p.setFont(labelFont);

    for (int i = 0; i < kStepCount; ++i) {
        const int cx = r.left() + stepW * i + stepW / 2;
        const bool done = activeIndex_ >= 0 && i < activeIndex_;
        const bool active = i == activeIndex_;
        const bool complete = activeIndex_ == 5 && i == 5;

        QColor fill(15, 23, 42);
        QColor border(51, 65, 85);
        if (failed_ && active) {
            fill = QColor(69, 10, 10);
            border = QColor(248, 113, 113);
        } else if (complete || done) {
            fill = QColor(4, 30, 18);
            border = QColor(74, 222, 128);
        } else if (active) {
            fill = QColor(8, 47, 73);
            border = QColor(34, 211, 238, static_cast<int>(160 + 80 * pulse));
        }

        const int radius = active ? 13 : 11;
        p.setPen(QPen(border, active ? 2.5 : 1.5));
        p.setBrush(fill);
        p.drawEllipse(QPoint(cx, cy), radius, radius);

        if (done || complete) {
            p.setPen(QPen(QColor(74, 222, 128), 2.0));
            p.drawLine(cx - 4, cy, cx - 1, cy + 4);
            p.drawLine(cx - 1, cy + 4, cx + 5, cy - 4);
        } else if (active && !failed_) {
            p.setBrush(QColor(34, 211, 238, static_cast<int>(120 + 80 * pulse)));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(cx, cy), 4, 4);
        }

        if (i < kStepCount - 1) {
            const bool linkDone = activeIndex_ > i;
            p.setPen(QPen(linkDone ? QColor(74, 222, 128, 180) : QColor(51, 65, 85, 140), 2.0));
            p.drawLine(cx + radius + 2, cy, cx + stepW - radius - 2, cy);
        }

        const QRect textRect(cx - stepW / 2 + 2, cy + 16, stepW - 4, 20);
        p.setPen(active ? QColor(224, 242, 254) : QColor(100, 116, 139));
        p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, QString::fromLatin1(kSteps[i]));
    }
}

}  // namespace sdv::dash::ui
