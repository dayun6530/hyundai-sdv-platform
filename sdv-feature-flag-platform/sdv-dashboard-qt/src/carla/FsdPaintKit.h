#pragma once

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace sdv::dash::carla::fsd {

struct FsdPalette {
    static QColor laneLeft();
    static QColor laneRight();
    static QColor laneCenter();
    static QColor corridorTop();
    static QColor corridorBottom();
    static QColor pathCurrent();
    static QColor pathPredicted();
    static QColor vehicleBox();
    static QColor pedestrianBox();
    static QColor bicycleBox();
    static QColor accHighlight();
};

void drawGlowPath(QPainter& p, const QPainterPath& path, const QColor& color, qreal width, int passes = 2);

void drawTeslaCornerBox(QPainter& p, const QRectF& box, const QColor& color, qreal cornerLen, qreal lineW = 2.2);

void drawGroundDiamond(QPainter& p, const QPointF& center, qreal radius, const QColor& color);

void drawLabelChip(QPainter& p, const QRectF& rect, const QString& text, const QColor& accent);

void drawManeuverArrow(QPainter& p, const QRectF& bounds, const QString& glyph, const QColor& color);

void drawLeaderLine(QPainter& p, const QPointF& from, const QPointF& to, const QColor& color);

qreal depthScale(qreal forwardM);

}  // namespace sdv::dash::carla::fsd
