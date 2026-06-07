#include "ui/SteeringWheelOverlay.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>

#include <QtMath>

namespace sdv::dash::ui {

SteeringWheelOverlay::SteeringWheelOverlay(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMinimumHeight(200);
    setMaximumHeight(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SteeringWheelOverlay::setChromeVisible(const bool visible)
{
    chromeVisible_ = visible;
    setVisible(visible);
    update();
}

void SteeringWheelOverlay::drawButtonCluster(QPainter& painter, const QRectF& area, const bool leftSide) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    const QColor pad(35, 38, 44);
    const QColor btn(48, 52, 60);
    painter.setBrush(pad);
    painter.drawRoundedRect(area, 10, 10);

    const int cols = 2;
    const int rows = 3;
    const double bw = area.width() / cols - 4;
    const double bh = area.height() / rows - 4;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            QRectF cell(area.left() + 3 + c * (bw + 4), area.top() + 3 + r * (bh + 4), bw, bh);
            painter.setBrush(btn);
            painter.drawRoundedRect(cell, 4, 4);
        }
    }
    Q_UNUSED(leftSide);
    painter.restore();
}

void SteeringWheelOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!chromeVisible_) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double w = width();
    const double h = height();
    const QPointF center(w * 0.5, h * 0.98);
    const double outerR = w * 0.42;
    const double rimW = qMax(22.0, outerR * 0.14);

    QRadialGradient shadow(center, outerR * 1.05);
    shadow.setColorAt(0.0, QColor(0, 0, 0, 120));
    shadow.setColorAt(0.55, QColor(0, 0, 0, 60));
    shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(shadow);
    painter.drawEllipse(center, outerR * 1.08, outerR * 0.55);

    QConicalGradient rimGrad(center, 90);
    rimGrad.setColorAt(0.0, QColor(58, 60, 66));
    rimGrad.setColorAt(0.25, QColor(32, 34, 38));
    rimGrad.setColorAt(0.5, QColor(24, 26, 30));
    rimGrad.setColorAt(0.75, QColor(32, 34, 38));
    rimGrad.setColorAt(1.0, QColor(58, 60, 66));

    QPen rimPen(QBrush(rimGrad), rimW, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(rimPen);
    painter.setBrush(Qt::NoBrush);
    QRectF rimRect(center.x() - outerR, center.y() - outerR, outerR * 2, outerR * 2);
    painter.drawArc(rimRect, 25 * 16, 130 * 16);

    QPen innerPen(QColor(70, 74, 82, 90), 2);
    painter.setPen(innerPen);
    const double innerR = outerR - rimW * 0.55;
    QRectF innerRect(center.x() - innerR, center.y() - innerR, innerR * 2, innerR * 2);
    painter.drawArc(innerRect, 30 * 16, 120 * 16);

    const double spokeR = innerR * 0.92;
    painter.setPen(QPen(QColor(40, 42, 48), 14, Qt::SolidLine, Qt::RoundCap));
    for (int i = -1; i <= 1; ++i) {
        const double angle = -90 + i * 38;
        const double rad = qDegreesToRadians(angle);
        const QPointF tip(center.x() + spokeR * qCos(rad), center.y() + spokeR * qSin(rad));
        painter.drawLine(center, tip);
    }

    const double hubR = outerR * 0.22;
    QRadialGradient hubGrad(center, hubR);
    hubGrad.setColorAt(0, QColor(44, 46, 52));
    hubGrad.setColorAt(0.7, QColor(28, 30, 34));
    hubGrad.setColorAt(1, QColor(18, 20, 24));
    painter.setPen(QPen(QColor(60, 64, 72), 2));
    painter.setBrush(hubGrad);
    painter.drawEllipse(center, hubR, hubR * 0.88);

    painter.setPen(QColor(200, 204, 212));
    QFont logoFont = font();
    logoFont.setPixelSize(qRound(hubR * 0.42));
    logoFont.setWeight(QFont::Bold);
    logoFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    painter.setFont(logoFont);
    const QRectF logoRect(center.x() - hubR, center.y() - hubR * 0.35, hubR * 2, hubR * 0.7);
    painter.drawText(logoRect, Qt::AlignCenter, QStringLiteral("HYUNDAI"));

    const double clusterW = outerR * 0.32;
    const double clusterH = outerR * 0.28;
    const double clusterY = center.y() - outerR * 0.55;
    drawButtonCluster(painter, QRectF(center.x() - outerR * 0.95, clusterY, clusterW, clusterH), true);
    drawButtonCluster(painter, QRectF(center.x() + outerR * 0.63, clusterY, clusterW, clusterH), false);
}

}  // namespace sdv::dash::ui
