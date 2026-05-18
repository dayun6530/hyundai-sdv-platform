#include "VehicleHUDView.h"

#include <cmath>

#include <QConicalGradient>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QEasingCurve>
#include <QStyleOptionGraphicsItem>
#include <QTimer>

namespace sdv::dash {

namespace {

bool mqttMainOn(const QJsonObject& mqttRoot, const char* featKey) {
    const QJsonObject feats = mqttRoot.value(QStringLiteral("features")).toObject();
    const QJsonObject f = feats.value(QLatin1String(featKey)).toObject();
    return f.value(QStringLiteral("main")).toBool(false);
}

QPointF lerp(const QPointF& a, const QPointF& b, qreal t) {
    return a + (b - a) * t;
}

bool featureOn(const QJsonObject& feature) {
    return feature.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0 &&
           !feature.value(QStringLiteral("kill_switch")).toBool();
}

bool mqttSubOnOrDefault(const QJsonObject& mqttFeat, const char* key, bool fallback) {
    const QJsonObject sub = mqttFeat.value(QStringLiteral("sub")).toObject();
    if (sub.contains(QLatin1String(key))) {
        return sub.value(QLatin1String(key)).toBool(fallback);
    }
    return fallback;
}

class HudCanvasItem final : public QGraphicsItem {
public:
    explicit HudCanvasItem(QJsonObject* car, QJsonObject* mqtt, qreal* simTime, qreal* roadPhase, qreal* upgradeGlow)
        : car_(car), mqtt_(mqtt), simTime_(simTime), roadPhase_(roadPhase), upgradeGlow_(upgradeGlow) {
        setZValue(0);
    }

    QRectF boundingRect() const override { return QRectF(0, 0, 1280, 720); }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF r = boundingRect();
        const qreal w = r.width();
        const qreal h = r.height();

        const QJsonObject ota = car_->value(QStringLiteral("ota")).toObject();
        const QJsonObject runtime = car_->value(QStringLiteral("runtime")).toObject();
        const bool procUp = runtime.contains(QStringLiteral("car_process_running"))
                                  ? runtime.value(QStringLiteral("car_process_running")).toBool()
                                  : true;
        const QJsonObject feats = car_->value(QStringLiteral("features")).toObject();
        const QJsonObject adasFleet = feats.value(QStringLiteral("ADAS")).toObject();
        const QJsonObject navFleet = feats.value(QStringLiteral("Navigation")).toObject();
        const QJsonObject energyFleet = feats.value(QStringLiteral("Energy")).toObject();

        const QJsonObject mqttFeats = mqtt_->value(QStringLiteral("features")).toObject();
        const QJsonObject adasM = mqttFeats.value(QStringLiteral("AdasFeature")).toObject();
        const QJsonObject navM = mqttFeats.value(QStringLiteral("NavigationFeature")).toObject();
        const QJsonObject evM = mqttFeats.value(QStringLiteral("EvEnergyFeature")).toObject();

        const bool adasOn = procUp && (mqttMainOn(*mqtt_, "AdasFeature") || featureOn(adasFleet));
        const bool navOn = procUp && (mqttMainOn(*mqtt_, "NavigationFeature") || featureOn(navFleet));
        const bool evOn = procUp && (mqttMainOn(*mqtt_, "EvEnergyFeature") || featureOn(energyFleet));
        const bool lka = adasOn && mqttSubOnOrDefault(adasM, "LKA", true);
        const bool fca = adasOn && mqttSubOnOrDefault(adasM, "FCA", true);
        const bool scc = adasOn && mqttSubOnOrDefault(adasM, "SCC", true);
        const bool guidance = navOn && mqttSubOnOrDefault(navM, "GUIDANCE", true);
        const bool regen = evOn && mqttSubOnOrDefault(evM, "REGEN", true);
        const bool shadow = adasFleet.value(QStringLiteral("shadow_enabled")).toBool();
        const bool kill = adasFleet.value(QStringLiteral("kill_switch")).toBool();

        const QString activeVersion = adasFleet.value(QStringLiteral("active_version")).toString();
        const QString phase = ota.value(QStringLiteral("ota_phase")).toString();
        const bool postOtaPolish = activeVersion.contains(QLatin1String("3")) ||
                                   phase.contains(QLatin1String("COMPLETE"), Qt::CaseInsensitive) ||
                                   phase.contains(QLatin1String("READY"), Qt::CaseInsensitive);

        qreal speed = procUp ? ota.value(QStringLiteral("speed")).toDouble(72.0) : 0.0;
        if (procUp && speed < 1.0) {
            speed = 72.0 + 4.0 * std::sin(*simTime_ * 0.42);
        }
        const int battery = qBound(0, qRound(ota.value(QStringLiteral("battery")).toDouble(82.0)), 100);
        const int range = ota.contains(QStringLiteral("range_km"))
                                ? qRound(ota.value(QStringLiteral("range_km")).toDouble(battery * 3.8))
                                : qRound(battery * 3.8);

        drawAtmosphere(p, r, postOtaPolish);

        const QPointF vanish(w * 0.515, h * 0.265);
        const QPointF roadLNear(w * 0.045, h * 0.77);
        const QPointF roadRNear(w * 0.955, h * 0.77);
        const QPointF roadLFar(w * 0.455, h * 0.34);
        const QPointF roadRFar(w * 0.58, h * 0.34);
        drawRoad(p, r, vanish, roadLNear, roadRNear, roadLFar, roadRFar);

        const qreal sway = lka ? 3.0 * std::sin(*simTime_ * 1.15) : 8.0 * std::sin(*simTime_ * 0.95);
        const QPointF laneLNear(w * 0.355 + sway, h * 0.755);
        const QPointF laneRNear(w * 0.645 - sway, h * 0.755);
        const QPointF laneLFar(w * 0.488 + sway * 0.08, h * 0.35);
        const QPointF laneRFar(w * 0.542 - sway * 0.08, h * 0.35);
        drawAutonomousLanes(p, laneLNear, laneLFar, laneRNear, laneRFar, lka, shadow, postOtaPolish);
        if (guidance) {
            drawRouteOverlay(p, laneLNear, laneLFar, laneRNear, laneRFar, postOtaPolish);
        }
        drawRoadMotion(p, laneLNear, laneLFar, laneRNear, laneRFar);
        drawDetectedVehicles(p, laneLNear, laneLFar, laneRNear, laneRFar, scc, fca);
        drawWindshieldHud(p, r, adasOn, lka, fca, scc, guidance, shadow, postOtaPolish);
        drawMinimap(p, QRectF(w - 315, 42, 270, 165), guidance, shadow, postOtaPolish);
        drawCockpitInterior(p, r);
        drawInstrumentCluster(p, QRectF(42, h - 185, 350, 132), speed, adasOn, lka, fca, scc);
        drawNavCard(p, QRectF(w * 0.40, h - 163, 270, 116), guidance);
        drawEvCard(p, QRectF(w - 348, h - 180, 300, 126), battery, range, regen);
        drawStatusBar(p, r, car_->value(QStringLiteral("car_id")).toString(QStringLiteral("car_1")), activeVersion, postOtaPolish);

        if (fca) {
            drawCollisionWarning(p, r);
        }
        if (shadow) {
            drawShadowNotice(p, r);
        }
        if (kill) {
            p->fillRect(r, QColor(185, 28, 28, 95));
            p->setPen(QPen(Qt::white, 2));
            p->setFont(QFont(QStringLiteral("Segoe UI"), 20, QFont::DemiBold));
            p->drawText(QRectF(0, 28, w, 42), Qt::AlignCenter, QStringLiteral("KILL SWITCH ACTIVE - MANUAL CONTROL REQUIRED"));
        }
    }

private:
    void drawAtmosphere(QPainter* p, const QRectF& r, bool upgraded) const {
        QLinearGradient bg(r.topLeft(), r.bottomLeft());
        bg.setColorAt(0.00, QColor(QStringLiteral("#08111e")));
        bg.setColorAt(0.42, upgraded ? QColor(QStringLiteral("#172337")) : QColor(QStringLiteral("#101827")));
        bg.setColorAt(1.00, QColor(QStringLiteral("#05080d")));
        p->fillRect(r, bg);

        const int pulseAlpha = qRound((upgraded ? 44 : 24) + (*upgradeGlow_) * (upgraded ? 30 : 14));
        QRadialGradient glow(QPointF(r.width() * 0.54, r.height() * 0.31), r.width() * 0.48);
        glow.setColorAt(0.0, QColor(98, 179, 255, pulseAlpha));
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p->fillRect(r, glow);

        p->setPen(Qt::NoPen);
        for (int i = 0; i < 22; ++i) {
            const qreal x = i * r.width() / 21.0;
            const qreal skyline = r.height() * (0.22 + 0.045 * std::sin(i * 1.7));
            p->setBrush(QColor(26, 36, 52, 120));
            p->drawRect(QRectF(x - 22, skyline, 36 + (i % 3) * 12, r.height() * 0.36 - skyline));
        }

        p->setPen(QPen(QColor(90, 105, 126, 75), 1));
        for (int i = 0; i < 12; ++i) {
            const qreal y = r.height() * 0.18 + i * 18;
            p->drawLine(QPointF(0, y), QPointF(r.width(), y + 10));
        }
    }

    void drawRoad(QPainter* p, const QRectF& r, const QPointF& vanish, const QPointF& lNear,
                  const QPointF& rNear, const QPointF& lFar, const QPointF& rFar) const {
        Q_UNUSED(vanish);
        QPolygonF road;
        road << lNear << lFar << rFar << rNear;
        QLinearGradient rg(QPointF(r.width() * 0.5, r.height() * 0.33), QPointF(r.width() * 0.5, r.height() * 0.78));
        rg.setColorAt(0.0, QColor(QStringLiteral("#1b222d")));
        rg.setColorAt(0.62, QColor(QStringLiteral("#202936")));
        rg.setColorAt(1.0, QColor(QStringLiteral("#151b24")));
        p->setPen(Qt::NoPen);
        p->setBrush(rg);
        p->drawPolygon(road);

        p->setPen(QPen(QColor(150, 160, 176, 75), 3, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(lNear, lFar);
        p->drawLine(rNear, rFar);

        p->setPen(QPen(QColor(168, 196, 217, 48), 2));
        for (int i = 1; i < 7; ++i) {
            const qreal t = i / 7.0;
            p->drawLine(lerp(lNear, lFar, t), lerp(rNear, rFar, t));
        }

        p->setPen(QPen(QColor(91, 113, 138, 140), 2));
        p->drawLine(QPointF(r.width() * 0.03, r.height() * 0.50), QPointF(lNear.x(), lNear.y()));
        p->drawLine(QPointF(r.width() * 0.97, r.height() * 0.50), QPointF(rNear.x(), rNear.y()));
    }

    void drawAutonomousLanes(QPainter* p, const QPointF& lNear, const QPointF& lFar,
                             const QPointF& rNear, const QPointF& rFar, bool lka, bool shadow, bool upgraded) const {
        const QColor base = lka ? (upgraded ? QColor(28, 214, 205) : QColor(35, 148, 225)) : QColor(115, 125, 140);
        for (int glow = 0; glow < 3; ++glow) {
            QColor c = base;
            c.setAlpha(glow == 0 ? 44 : (glow == 1 ? 82 : 230));
            p->setPen(QPen(c, glow == 0 ? 18 : (glow == 1 ? 8 : 3.3), Qt::SolidLine, Qt::RoundCap));
            QPainterPath left;
            left.moveTo(lNear);
            left.cubicTo(lerp(lNear, lFar, 0.38) + QPointF(-18, -8), lerp(lNear, lFar, 0.70), lFar);
            QPainterPath right;
            right.moveTo(rNear);
            right.cubicTo(lerp(rNear, rFar, 0.38) + QPointF(18, -8), lerp(rNear, rFar, 0.70), rFar);
            p->drawPath(left);
            p->drawPath(right);
        }

        if (shadow) {
            QColor sc(QStringLiteral("#cbd5e1"));
            sc.setAlpha(125);
            p->setPen(QPen(sc, 2.2, Qt::DashLine, Qt::RoundCap));
            p->drawLine(lNear + QPointF(22, -4), lFar + QPointF(5, 0));
            p->drawLine(rNear - QPointF(22, 4), rFar - QPointF(5, 0));
        }
    }

    void drawRouteOverlay(QPainter* p, const QPointF& lNear, const QPointF& lFar,
                          const QPointF& rNear, const QPointF& rFar, bool upgraded) const {
        const QPointF cNear = lerp(lNear, rNear, 0.5);
        const QPointF cFar = lerp(lFar, rFar, 0.5);
        QPainterPath route;
        route.moveTo(cNear + QPointF(0, -8));
        route.cubicTo(lerp(cNear, cFar, 0.30) + QPointF(-28, -8),
                      lerp(cNear, cFar, 0.64) + QPointF(18, -6),
                      cFar + QPointF(0, 10));

        QColor glow = upgraded ? QColor(56, 189, 248, 66) : QColor(59, 130, 246, 54);
        p->setPen(QPen(glow, upgraded ? 36 : 24, Qt::SolidLine, Qt::RoundCap));
        p->drawPath(route);
        p->setPen(QPen(upgraded ? QColor(QStringLiteral("#5eead4")) : QColor(QStringLiteral("#60a5fa")),
                       upgraded ? 7.5 : 5.0, Qt::SolidLine, Qt::RoundCap));
        p->drawPath(route);
    }

    void drawRoadMotion(QPainter* p, const QPointF& lNear, const QPointF& lFar,
                        const QPointF& rNear, const QPointF& rFar) const {
        p->setPen(Qt::NoPen);
        const qreal phase = std::fmod(*roadPhase_ * 0.012, 0.14);
        for (int i = 0; i < 10; ++i) {
            const qreal t = qBound<qreal>(0.0, i * 0.12 + phase, 0.98);
            const QPointF left = lerp(lNear, lFar, t);
            const QPointF right = lerp(rNear, rFar, t);
            const QPointF mid = lerp(left, right, 0.5);
            const qreal width = 30.0 * (1.0 - t) + 4.0;
            QColor c(226, 232, 240, qRound(120 * (1.0 - t)));
            p->setBrush(c);
            p->drawRoundedRect(QRectF(mid.x() - width / 2, mid.y() - 2, width, 4), 2, 2);
        }
    }

    void drawDetectedVehicles(QPainter* p, const QPointF& lNear, const QPointF& lFar,
                              const QPointF& rNear, const QPointF& rFar, bool scc, bool fca) const {
        auto drawCar = [&](qreal lane, qreal depth, QColor body, const QString& label) {
            const QPointF left = lerp(lNear, lFar, depth);
            const QPointF right = lerp(rNear, rFar, depth);
            const QPointF pos = lerp(left, right, lane);
            const qreal scale = 0.28 + (1.0 - depth) * 1.25;
            const QRectF car(-28 * scale, -34 * scale, 56 * scale, 52 * scale);

            p->save();
            p->translate(pos);
            p->setPen(QPen(scc ? QColor(94, 234, 212, 210) : QColor(148, 163, 184, 170), 2));
            if (scc) {
                p->setBrush(QColor(20, 184, 166, 30));
                p->drawRoundedRect(car.adjusted(-10, -10, 10, 10), 10, 10);
            }
            p->setBrush(body);
            p->drawRoundedRect(car, 8, 8);
            p->setBrush(QColor(15, 23, 42, 170));
            p->drawRoundedRect(car.adjusted(8 * scale, 8 * scale, -8 * scale, -24 * scale), 4, 4);
            p->restore();

            p->setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
            p->setPen(fca ? QColor(QStringLiteral("#fecaca")) : QColor(QStringLiteral("#cbd5e1")));
            p->drawText(QRectF(pos.x() - 70, pos.y() + 22, 140, 18), Qt::AlignCenter, label);
        };

        drawCar(0.52, 0.52 + 0.035 * std::sin(*simTime_ * 0.7), QColor(QStringLiteral("#d6dee8")), QStringLiteral("LOCK"));
        drawCar(0.20, 0.67, QColor(QStringLiteral("#8996a8")), QStringLiteral("OBJ"));
        drawCar(0.82, 0.74, QColor(QStringLiteral("#6f7d90")), QStringLiteral("OBJ"));
    }

    void drawWindshieldHud(QPainter* p, const QRectF& r, bool adasOn, bool lka, bool fca,
                           bool scc, bool guidance, bool shadow, bool upgraded) const {
        auto pill = [&](const QRectF& box, const QString& label, bool on, QColor accent) {
            p->setPen(QPen(QColor(148, 163, 184, 90), 1));
            p->setBrush(QColor(10, 18, 30, 138));
            p->drawRoundedRect(box, 12, 12);
            p->setPen(on ? accent : QColor(QStringLiteral("#64748b")));
            p->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
            p->drawText(box, Qt::AlignCenter, label);
        };

        pill(QRectF(r.width() * 0.44, 26, 155, 32), adasOn ? QStringLiteral("AUTO DRIVE ACTIVE") : QStringLiteral("ADAS STANDBY"),
             adasOn, QColor(QStringLiteral("#34d399")));
        pill(QRectF(r.width() * 0.33, 66, 92, 28), QStringLiteral("LKA"), lka, QColor(QStringLiteral("#5eead4")));
        pill(QRectF(r.width() * 0.43, 66, 92, 28), QStringLiteral("ACC"), scc, QColor(QStringLiteral("#93c5fd")));
        pill(QRectF(r.width() * 0.53, 66, 92, 28), QStringLiteral("FCA"), fca, QColor(QStringLiteral("#fca5a5")));
        pill(QRectF(r.width() * 0.63, 66, 108, 28), upgraded ? QStringLiteral("OTA vNEXT") : QStringLiteral("OTA BASE"), upgraded,
             QColor(QStringLiteral("#67e8f9")));

        if (guidance) {
            p->setPen(QColor(QStringLiteral("#bfdbfe")));
            p->setFont(QFont(QStringLiteral("Segoe UI"), 13, QFont::DemiBold));
            p->drawText(QRectF(r.width() * 0.43, 108, 260, 34), Qt::AlignCenter, QStringLiteral("↱ Turn right in 120 m"));
        }
        if (shadow) {
            p->setPen(QColor(QStringLiteral("#e2e8f0")));
            p->setFont(QFont(QStringLiteral("Segoe UI"), 10));
            p->drawText(QRectF(r.width() * 0.38, 144, 360, 28), Qt::AlignCenter,
                        QStringLiteral("Shadow lane overlay enabled"));
        }
    }

    void drawMinimap(QPainter* p, const QRectF& box, bool guidance, bool shadow, bool upgraded) const {
        p->setPen(QPen(QColor(148, 163, 184, 85), 1));
        p->setBrush(QColor(8, 15, 26, 168));
        p->drawRoundedRect(box, 16, 16);
        p->setClipRect(box.adjusted(8, 8, -8, -8));

        p->setPen(QPen(QColor(71, 85, 105, 160), 2));
        p->drawLine(QPointF(box.center().x(), box.top() + 16), QPointF(box.center().x(), box.bottom() - 18));
        p->drawLine(QPointF(box.left() + 24, box.center().y()), QPointF(box.right() - 22, box.center().y() - 18));

        QPainterPath route;
        route.moveTo(box.center().x(), box.bottom() - 24);
        route.cubicTo(box.center().x() - 18, box.center().y() + 18, box.center().x() + 56, box.center().y() - 6,
                      box.right() - 42, box.top() + 30);
        p->setPen(QPen(guidance ? QColor(QStringLiteral("#38bdf8")) : QColor(100, 116, 139), upgraded ? 5 : 3,
                       Qt::SolidLine, Qt::RoundCap));
        p->drawPath(route);

        p->setPen(Qt::NoPen);
        p->setBrush(QColor(QStringLiteral("#f8fafc")));
        p->drawRoundedRect(QRectF(box.center().x() - 8, box.bottom() - 46, 16, 28), 4, 4);
        p->setBrush(QColor(94, 234, 212, 150));
        p->drawEllipse(QPointF(box.center().x() + 26, box.center().y() - 8), 5, 5);
        p->drawEllipse(QPointF(box.center().x() - 38, box.center().y() + 14), 4, 4);

        p->setClipping(false);
        p->setPen(QColor(QStringLiteral("#cbd5e1")));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p->drawText(box.adjusted(12, 8, -12, -8), Qt::AlignTop | Qt::AlignLeft, shadow ? QStringLiteral("MINIMAP | SHADOW") : QStringLiteral("MINIMAP"));
    }

    void drawCockpitInterior(QPainter* p, const QRectF& r) const {
        const qreal w = r.width();
        const qreal h = r.height();

        QLinearGradient dash(0, h * 0.61, 0, h);
        dash.setColorAt(0.0, QColor(5, 8, 13, 0));
        dash.setColorAt(0.24, QColor(9, 13, 20, 225));
        dash.setColorAt(1.0, QColor(2, 4, 8, 255));
        p->fillRect(QRectF(0, h * 0.58, w, h * 0.42), dash);

        p->setPen(QPen(QColor(96, 165, 250, 36), 1.5));
        p->drawArc(QRectF(w * 0.15, h * 0.60, w * 0.70, h * 0.44), 8 * 16, 164 * 16);

        QRadialGradient wheel(QPointF(w * 0.235, h * 0.86), 168);
        wheel.setColorAt(0.0, QColor(8, 12, 18, 180));
        wheel.setColorAt(0.48, QColor(12, 17, 25, 240));
        wheel.setColorAt(0.64, QColor(39, 48, 63, 230));
        wheel.setColorAt(0.72, QColor(7, 10, 16, 245));
        wheel.setColorAt(1.0, QColor(0, 0, 0, 0));
        p->setBrush(wheel);
        p->setPen(QPen(QColor(91, 104, 124, 90), 12));
        p->drawEllipse(QPointF(w * 0.235, h * 0.86), 145, 145);
        p->setPen(QPen(QColor(17, 24, 39, 220), 22, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(w * 0.235, h * 0.86), QPointF(w * 0.235, h * 0.98));

        p->setPen(QPen(QColor(203, 213, 225, 28), 1));
        for (int i = 0; i < 6; ++i) {
            p->drawLine(QPointF(w * 0.02, h * (0.64 + i * 0.025)), QPointF(w * 0.98, h * (0.60 + i * 0.018)));
        }
    }

    void glassCard(QPainter* p, const QRectF& box, QColor accent = QColor(96, 165, 250, 90)) const {
        p->setPen(QPen(QColor(148, 163, 184, 86), 1));
        p->setBrush(QColor(8, 13, 22, 174));
        p->drawRoundedRect(box, 18, 18);
        p->setPen(QPen(accent, 1.8));
        p->drawLine(box.topLeft() + QPointF(18, 0), box.topRight() - QPointF(18, 0));
    }

    void drawInstrumentCluster(QPainter* p, const QRectF& box, qreal speed, bool adasOn,
                               bool lka, bool fca, bool scc) const {
        glassCard(p, box, QColor(56, 189, 248, 110));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 34, QFont::Bold));
        p->setPen(QColor(QStringLiteral("#f8fafc")));
        p->drawText(QRectF(box.left() + 22, box.top() + 18, 118, 54), Qt::AlignLeft | Qt::AlignVCenter,
                    QString::number(qRound(speed)));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 10));
        p->setPen(QColor(QStringLiteral("#cbd5e1")));
        p->drawText(QRectF(box.left() + 118, box.top() + 42, 70, 18), QStringLiteral("km/h"));

        const QPointF car(box.left() + 226, box.top() + 68);
        p->setPen(QPen(lka ? QColor(QStringLiteral("#5eead4")) : QColor(QStringLiteral("#64748b")), 2));
        p->drawLine(car + QPointF(-46, 44), car + QPointF(-18, -36));
        p->drawLine(car + QPointF(46, 44), car + QPointF(18, -36));
        p->setPen(QPen(fca ? QColor(QStringLiteral("#f87171")) : QColor(QStringLiteral("#60a5fa")), 2));
        p->setBrush(QColor(QStringLiteral("#e2e8f0")));
        p->drawRoundedRect(QRectF(car.x() - 14, car.y() - 23, 28, 46), 6, 6);

        p->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p->setPen(adasOn ? QColor(QStringLiteral("#34d399")) : QColor(QStringLiteral("#94a3b8")));
        p->drawText(QRectF(box.left() + 20, box.bottom() - 34, 210, 18),
                    QStringLiteral("ADAS %1 | LKA %2 | ACC %3").arg(adasOn ? QStringLiteral("ARM") : QStringLiteral("STBY"),
                                                                    lka ? QStringLiteral("ON") : QStringLiteral("OFF"),
                                                                    scc ? QStringLiteral("ON") : QStringLiteral("OFF")));
    }

    void drawNavCard(QPainter* p, const QRectF& box, bool guidance) const {
        glassCard(p, box, QColor(59, 130, 246, 105));
        p->setPen(guidance ? QColor(QStringLiteral("#93c5fd")) : QColor(QStringLiteral("#64748b")));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 28, QFont::Bold));
        p->drawText(QRectF(box.left() + 20, box.top() + 12, 58, 54), Qt::AlignCenter, QStringLiteral("↱"));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 12, QFont::DemiBold));
        p->setPen(QColor(QStringLiteral("#e2e8f0")));
        p->drawText(QRectF(box.left() + 84, box.top() + 20, box.width() - 100, 24),
                    guidance ? QStringLiteral("120 m") : QStringLiteral("Route standby"));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        p->setPen(QColor(QStringLiteral("#94a3b8")));
        p->drawText(QRectF(box.left() + 84, box.top() + 48, box.width() - 100, 20),
                    guidance ? QStringLiteral("Right turn / HMI route overlay") : QStringLiteral("Navigation feature disabled"));
        p->setPen(QPen(QColor(QStringLiteral("#38bdf8")), 3, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(box.left() + 22, box.bottom() - 28), QPointF(box.right() - 26, box.bottom() - 28));
    }

    void drawEvCard(QPainter* p, const QRectF& box, int battery, int range, bool regen) const {
        glassCard(p, box, QColor(20, 184, 166, 110));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 10, QFont::DemiBold));
        p->setPen(QColor(QStringLiteral("#a7f3d0")));
        p->drawText(QRectF(box.left() + 18, box.top() + 14, 180, 22), QStringLiteral("EV ENERGY"));

        const QRectF batt(box.left() + 18, box.top() + 48, box.width() - 36, 18);
        p->setPen(QPen(QColor(148, 163, 184, 110), 1));
        p->setBrush(QColor(15, 23, 42, 170));
        p->drawRoundedRect(batt, 8, 8);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(QStringLiteral("#2dd4bf")));
        p->drawRoundedRect(QRectF(batt.left(), batt.top(), batt.width() * battery / 100.0, batt.height()), 8, 8);

        p->setFont(QFont(QStringLiteral("Segoe UI"), 18, QFont::Bold));
        p->setPen(QColor(QStringLiteral("#f8fafc")));
        p->drawText(QRectF(box.left() + 18, box.top() + 74, 120, 34), QStringLiteral("%1%").arg(battery));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 11));
        p->setPen(QColor(QStringLiteral("#cbd5e1")));
        p->drawText(QRectF(box.left() + 142, box.top() + 80, 130, 24), QStringLiteral("%1 km").arg(range));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        p->setPen(regen ? QColor(QStringLiteral("#5eead4")) : QColor(QStringLiteral("#64748b")));
        p->drawText(QRectF(box.left() + 18, box.bottom() - 26, box.width() - 32, 20),
                    regen ? QStringLiteral("Regenerative flow active") : QStringLiteral("Regen standby"));
    }

    void drawStatusBar(QPainter* p, const QRectF& r, const QString& carId, const QString& version, bool upgraded) const {
        p->setPen(QColor(QStringLiteral("#cbd5e1")));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 10, QFont::DemiBold));
        p->drawText(QRectF(32, 22, 320, 24), QStringLiteral("Hyundai SDV Cockpit | %1").arg(carId));
        p->setPen(upgraded ? QColor(QStringLiteral("#5eead4")) : QColor(QStringLiteral("#93c5fd")));
        p->drawText(QRectF(r.width() - 505, 22, 190, 22),
                    QStringLiteral("ADAS %1").arg(version.isEmpty() ? QStringLiteral("base") : version));
    }

    void drawCollisionWarning(QPainter* p, const QRectF& r) const {
        const qreal pulse = (std::sin(*simTime_ * 2.8) + 1.0) * 0.5;
        if (pulse < 0.55) {
            return;
        }
        p->setPen(QPen(QColor(248, 113, 113, qRound(120 + pulse * 90)), 9));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(r.adjusted(20, 18, -20, -18), 24, 24);
        p->setPen(QColor(QStringLiteral("#fecaca")));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 12, QFont::DemiBold));
        p->drawText(QRectF(r.width() * 0.40, r.height() * 0.22, 260, 34), Qt::AlignCenter, QStringLiteral("FRONT OBJECT MONITORING"));
    }

    void drawShadowNotice(QPainter* p, const QRectF& r) const {
        QRectF box(r.width() * 0.38, 168, 310, 34);
        p->setPen(QPen(QColor(226, 232, 240, 75), 1));
        p->setBrush(QColor(15, 23, 42, 155));
        p->drawRoundedRect(box, 12, 12);
        p->setPen(QColor(QStringLiteral("#e2e8f0")));
        p->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        const bool deviating = std::sin(*simTime_ * 0.9) > 0.72;
        p->drawText(box, Qt::AlignCenter, deviating ? QStringLiteral("Shadow deviation detected") : QStringLiteral("Shadow validation overlay"));
    }

private:
    QJsonObject* car_;
    QJsonObject* mqtt_;
    qreal* simTime_;
    qreal* roadPhase_;
    qreal* upgradeGlow_;
};

}  // namespace

VehicleHUDView::VehicleHUDView(QWidget* parent) : QGraphicsView(parent) {
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(0, 0, 1280, 720);
    setScene(scene_);
    auto* item = new HudCanvasItem(&car_, &mqtt_, &simTime_, &roadPhase_, &upgradeGlow_);
    scene_->addItem(item);

    connect(&timer_, &QTimer::timeout, this, &VehicleHUDView::onTick);
    timer_.start(33);

    glowAnim_ = new QPropertyAnimation(this, "upgradeGlow", this);
    glowAnim_->setStartValue(0.18);
    glowAnim_->setEndValue(1.0);
    glowAnim_->setDuration(2600);
    glowAnim_->setLoopCount(-1);
    glowAnim_->setEasingCurve(QEasingCurve::InOutSine);
    glowAnim_->start();
}

void VehicleHUDView::setUpgradeGlow(qreal value) {
    upgradeGlow_ = value;
    if (scene_) {
        scene_->invalidate(scene_->sceneRect());
    }
}

void VehicleHUDView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void VehicleHUDView::ingestFleetCar(const QJsonObject& carState) {
    car_ = carState;
    scene_->invalidate(scene_->sceneRect());
}

void VehicleHUDView::ingestMqttFlags(const QJsonObject& mqttEnvelope) {
    mqtt_ = mqttEnvelope;
    scene_->invalidate(scene_->sceneRect());
}

void VehicleHUDView::onTick() {
    simTime_ += 0.033;
    double spd = car_.value(QStringLiteral("ota")).toObject().value(QStringLiteral("speed")).toDouble(70.0);
    if (spd < 1.0) {
        spd = 70.0;
    }
    roadPhase_ += spd * 0.018;
    scene_->invalidate(scene_->sceneRect());
}

}  // namespace sdv::dash
