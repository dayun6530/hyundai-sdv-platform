#include "carla/FsdPaintKit.h"

#include <QPainterPath>
#include <QtMath>

namespace sdv::dash::carla::fsd {

QColor FsdPalette::laneLeft() { return QColor(230, 240, 255, 235); }
QColor FsdPalette::laneRight() { return QColor(230, 240, 255, 235); }
QColor FsdPalette::laneCenter() { return QColor(255, 255, 255, 180); }
QColor FsdPalette::corridorTop() { return QColor(59, 130, 246, 28); }
QColor FsdPalette::corridorBottom() { return QColor(34, 197, 94, 95); }
QColor FsdPalette::pathCurrent() { return QColor(56, 189, 248, 245); }
QColor FsdPalette::pathPredicted() { return QColor(163, 230, 53, 200); }
QColor FsdPalette::vehicleBox() { return QColor(56, 189, 248, 240); }
QColor FsdPalette::pedestrianBox() { return QColor(251, 146, 60, 240); }
QColor FsdPalette::bicycleBox() { return QColor(250, 204, 21, 240); }
QColor FsdPalette::accHighlight() { return QColor(251, 191, 36, 255); }

void drawGlowPath(QPainter& p, const QPainterPath& path, const QColor& color, const qreal width, const int passes)
{
    for (int i = passes; i >= 1; --i) {
        QPen glow(color);
        glow.setWidthF(width * (0.55 + 0.45 * i));
        glow.setCapStyle(Qt::RoundCap);
        glow.setJoinStyle(Qt::RoundJoin);
        QColor c = color;
        c.setAlpha(color.alpha() / (i + 1));
        glow.setColor(c);
        p.setPen(glow);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
}

void drawTeslaCornerBox(QPainter& p, const QRectF& box, const QColor& color, const qreal cornerLen, const qreal lineW)
{
    QPen pen(color, lineW);
    pen.setCapStyle(Qt::SquareCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal l = cornerLen;
    const QPointF tl = box.topLeft();
    const QPointF tr = box.topRight();
    const QPointF bl = box.bottomLeft();
    const QPointF br = box.bottomRight();

    p.drawLine(tl, tl + QPointF(l, 0));
    p.drawLine(tl, tl + QPointF(0, l));
    p.drawLine(tr, tr + QPointF(-l, 0));
    p.drawLine(tr, tr + QPointF(0, l));
    p.drawLine(bl, bl + QPointF(l, 0));
    p.drawLine(bl, bl + QPointF(0, -l));
    p.drawLine(br, br + QPointF(-l, 0));
    p.drawLine(br, br + QPointF(0, -l));

    QColor fill = color;
    fill.setAlpha(28);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRect(box);
}

void drawGroundDiamond(QPainter& p, const QPointF& center, const qreal radius, const QColor& color)
{
    QPolygonF diamond;
    diamond << center + QPointF(0, -radius * 0.35) << center + QPointF(radius, 0) << center + QPointF(0, radius * 0.55)
            << center + QPointF(-radius, 0);
    p.setPen(QPen(color, 1.5));
    QColor fill = color;
    fill.setAlpha(40);
    p.setBrush(fill);
    p.drawPolygon(diamond);
}

void drawLabelChip(QPainter& p, const QRectF& rect, const QString& text, const QColor& accent)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(8, 12, 24, 210));
    p.drawRoundedRect(rect, 4, 4);
    p.setPen(QPen(accent, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect, 4, 4);

    QFont f = p.font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(248, 250, 252));
    p.drawText(rect.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void drawManeuverArrow(QPainter& p, const QRectF& bounds, const QString& glyph, const QColor& color)
{
    const QPointF c = bounds.center();
    const qreal s = qMin(bounds.width(), bounds.height()) * 0.32;
    QPainterPath path;

    if (glyph.contains(QLatin1String("←")) || glyph == QStringLiteral("L")) {
        path.moveTo(c + QPointF(s * 0.4, 0));
        path.lineTo(c + QPointF(-s * 0.5, 0));
        path.lineTo(c + QPointF(-s * 0.15, -s * 0.35));
        path.moveTo(c + QPointF(-s * 0.5, 0));
        path.lineTo(c + QPointF(-s * 0.15, s * 0.35));
    } else if (glyph.contains(QLatin1String("→")) || glyph == QStringLiteral("R")) {
        path.moveTo(c + QPointF(-s * 0.4, 0));
        path.lineTo(c + QPointF(s * 0.5, 0));
        path.lineTo(c + QPointF(s * 0.15, -s * 0.35));
        path.moveTo(c + QPointF(s * 0.5, 0));
        path.lineTo(c + QPointF(s * 0.15, s * 0.35));
    } else if (glyph.contains(QLatin1String("↶"))) {
        path.addEllipse(c, s * 0.45, s * 0.45);
    } else {
        path.moveTo(c + QPointF(0, -s * 0.55));
        path.lineTo(c + QPointF(0, s * 0.35));
        path.lineTo(c + QPointF(-s * 0.32, s * 0.05));
        path.moveTo(c + QPointF(0, s * 0.35));
        path.lineTo(c + QPointF(s * 0.32, s * 0.05));
    }

    QPen pen(color, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    drawGlowPath(p, path, color, 10.0, 2);
}

void drawLeaderLine(QPainter& p, const QPointF& from, const QPointF& to, const QColor& color)
{
    QPen pen(color, 1.2, Qt::DashLine);
    pen.setDashPattern({3.0, 5.0});
    p.setPen(pen);
    p.drawLine(from, to);
}

qreal depthScale(const qreal forwardM)
{
    return qBound(0.35, 1.0 - forwardM / 85.0, 1.0);
}

}  // namespace sdv::dash::carla::fsd
