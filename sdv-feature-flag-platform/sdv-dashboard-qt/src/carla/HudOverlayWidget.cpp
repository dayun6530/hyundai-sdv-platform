#include "carla/HudOverlayWidget.h"

#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ui/DashboardTheme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QtMath>

using sdv::dash::ui::DashboardTheme;

namespace sdv::dash::carla {

namespace {

void styleHudLabel(QLabel* label, const QString& css)
{
    label->setStyleSheet(css);
    label->setAttribute(Qt::WA_TranslucentBackground, true);
}

QFrame* glassChip(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setObjectName(QStringLiteral("carlaHudChip"));
    f->setStyleSheet(DashboardTheme::hudChipStyle());
    return f;
}

}  // namespace

HudOverlayWidget::HudOverlayWidget(QWidget* parent) : QFrame(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setStyleSheet(QStringLiteral("background: transparent; border: 0;"));

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(16, 12, 16, 16);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    auto* speedChip = glassChip(this);
    auto* speedLay = new QHBoxLayout(speedChip);
    speedLay->setContentsMargins(12, 8, 12, 8);
    speedValue_ = new QLabel(QStringLiteral("0"), speedChip);
    speedValue_->setObjectName(QStringLiteral("speedValue"));
    styleHudLabel(speedValue_, DashboardTheme::hudSpeedStyle());
    auto* unit = new QLabel(QStringLiteral("km/h"), speedChip);
    styleHudLabel(unit, QStringLiteral("color: #cbd5e1; font-size: 14px; font-weight: 600;"));
    speedLimit_ = new QLabel(QStringLiteral("80"), speedChip);
    speedLimit_->setFixedSize(40, 40);
    speedLimit_->setAlignment(Qt::AlignCenter);
    speedLimit_->setStyleSheet(QStringLiteral(
        "color: #fecaca; font-weight: 800; font-size: 13px;"
        "background: rgba(40,8,8,0.65); border: 2px solid #ef4444; border-radius: 20px;"));
    speedLay->addWidget(speedValue_);
    speedLay->addWidget(unit, 0, Qt::AlignBottom);
    speedLay->addWidget(speedLimit_, 0, Qt::AlignBottom);
    grid->addWidget(speedChip, 0, 0, Qt::AlignLeft | Qt::AlignTop);

    auto* statusCol = new QVBoxLayout();
    adasState_ = new QLabel(QStringLiteral("ADAS · ACTIVE"), this);
    styleHudLabel(adasState_, QStringLiteral(
        "color: #67e8f9; font-size: 11px; font-weight: 800; letter-spacing: 1.2px;"
        "background: rgba(8,20,40,0.88); border: 1px solid rgba(56,189,248,0.55);"
        "border-radius: 10px; padding: 8px 12px;"));
    autoDrive_ = new QLabel(QStringLiteral("AUTO DRIVE ACTIVE"), this);
    styleHudLabel(autoDrive_, QStringLiteral(
        "color: #4ade80; font-size: 13px; font-weight: 900; letter-spacing: 1px;"
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(4,30,18,0.9), stop:1 rgba(4,16,10,0.85));"
        "border: 1px solid rgba(74,222,128,0.65); border-radius: 10px; padding: 8px 14px;"));
    statusCol->addWidget(adasState_, 0, Qt::AlignRight);
    statusCol->addWidget(autoDrive_, 0, Qt::AlignRight);
    grid->addLayout(statusCol, 0, 2, Qt::AlignRight | Qt::AlignTop);

    gpsLine_ = new QLabel(QStringLiteral("GPS · —"), this);
    styleHudLabel(gpsLine_, QStringLiteral(
        "color: #e2e8f0; font-size: 11px; font-weight: 600;"
        "background: rgba(15,23,42,0.72); border-radius: 8px; padding: 6px 10px;"));
    grid->addWidget(gpsLine_, 1, 0, Qt::AlignLeft | Qt::AlignBottom);

    controlLine_ = new QLabel(QStringLiteral("THR 0 · BRK 0 · STR 0"), this);
    styleHudLabel(controlLine_, QStringLiteral(
        "color: #a5f3fc; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;"
        "background: rgba(8,20,40,0.85); border-radius: 8px; padding: 5px 10px;"));
    headingLine_ = new QLabel(QStringLiteral("HDG —°"), this);
    styleHudLabel(headingLine_, QStringLiteral(
        "color: #e2e8f0; font-size: 10px; font-weight: 700;"
        "background: rgba(15,23,42,0.72); border-radius: 8px; padding: 5px 10px;"));
    grid->addWidget(controlLine_, 2, 0, Qt::AlignLeft | Qt::AlignBottom);
    grid->addWidget(headingLine_, 2, 2, Qt::AlignRight | Qt::AlignBottom);

    auto* navChip = glassChip(this);
    auto* navLay = new QHBoxLayout(navChip);
    navLay->setContentsMargins(14, 8, 14, 8);
    maneuverArrows_ = new QLabel(QStringLiteral("↑  ↑  ↑"), navChip);
    styleHudLabel(maneuverArrows_, QStringLiteral(
        "color: #f8fafc; font-size: 16px; font-weight: 800; letter-spacing: 3px;"));
    maneuverDist_ = new QLabel(QStringLiteral("120 m"), navChip);
    styleHudLabel(maneuverDist_, QStringLiteral("color: #f8fafc; font-size: 18px; font-weight: 800;"));
    maneuverHint_ = new QLabel(QStringLiteral("CARLA · 3D Live"), navChip);
    styleHudLabel(maneuverHint_, QStringLiteral("color: #94a3b8; font-size: 10px;"));
    navLay->addWidget(maneuverArrows_);
    navLay->addWidget(maneuverDist_);
    navLay->addWidget(maneuverHint_);
    grid->addWidget(navChip, 1, 0, 1, 3, Qt::AlignHCenter | Qt::AlignBottom);

    auto* gearCol = new QVBoxLayout();
    const QString gearIdle = QStringLiteral(
        "color: #64748b; font-size: 15px; font-weight: 700; padding: 4px 8px; border-radius: 6px;");
    const QString gearActive = QStringLiteral(
        "color: #f8fafc; font-size: 15px; font-weight: 800; padding: 4px 8px;"
        "background: rgba(37,99,235,0.65); border: 1px solid rgba(96,165,250,0.7); border-radius: 6px;");
    gearP_ = new QLabel(QStringLiteral("P"), this);
    gearR_ = new QLabel(QStringLiteral("R"), this);
    gearN_ = new QLabel(QStringLiteral("N"), this);
    gearD_ = new QLabel(QStringLiteral("D"), this);
    for (auto* g : {gearP_, gearR_, gearN_, gearD_}) {
        g->setStyleSheet(gearIdle);
        gearCol->addWidget(g, 0, Qt::AlignRight);
    }
    gearD_->setStyleSheet(gearActive);
    grid->addLayout(gearCol, 0, 1, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
}

void HudOverlayWidget::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(pipeline_->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, &HudOverlayWidget::onEgoChanged);
    onEgoChanged();
}

void HudOverlayWidget::setVehicleId(const QString& vehicleId)
{
    vehicleId_ = vehicleId.trimmed().isEmpty() ? QStringLiteral("car_1") : vehicleId.trimmed();
    refreshGpsLine();
}

void HudOverlayWidget::updateFromCarState(const QJsonObject& carState, const bool /*mqttConnected*/)
{
    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
    adasActive_ = adas.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0 ||
                  !adas.value(QStringLiteral("active_version")).toString().isEmpty();
    autoActive_ = adasActive_;
    adasState_->setText(adasActive_ ? QStringLiteral("ADAS · ACTIVE") : QStringLiteral("ADAS · STANDBY"));
    autoDrive_->setText(autoActive_ ? QStringLiteral("AUTO DRIVE ACTIVE") : QStringLiteral("MANUAL MODE"));

    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    const double speed = ota.value(QStringLiteral("speed")).toDouble();
    if (speed >= 1.0) {
        speedKmh_ = speed;
        speedValue_->setText(QString::number(qRound(speedKmh_)));
    }
    update();
}

void HudOverlayWidget::updateNavGuidance(const QString& maneuverArrows, const QString& distanceText)
{
    if (!maneuverArrows.isEmpty()) {
        maneuverArrows_->setText(maneuverArrows);
    }
    if (!distanceText.isEmpty()) {
        maneuverDist_->setText(distanceText);
    }
}

void HudOverlayWidget::onEgoChanged()
{
    if (!pipeline_) {
        return;
    }
    const auto& pose = pipeline_->egoViewModel()->pose();
    if (!pose.isValid()) {
        return;
    }
    speedKmh_ = qMax(0.0, pose.speedMps * 3.6);
    speedValue_->setText(QString::number(qRound(speedKmh_)));
    headingDeg_ = pose.yawRad * 180.0 / M_PI;
    if (headingDeg_ < 0.0) {
        headingDeg_ += 360.0;
    }
    headingLine_->setText(QStringLiteral("HDG %1°").arg(qRound(headingDeg_)));
    refreshGpsLine();
    updateGearFromPipeline(pose.speedMps);
    update();
}

void HudOverlayWidget::updateControlTelemetry(const double throttle, const double brake, const double steer,
                                              const bool autopilot, const bool /*emergencyStop*/)
{
    controlLine_->setText(
        QStringLiteral("THR %1% · BRK %2% · STR %3%")
            .arg(qRound(throttle * 100))
            .arg(qRound(brake * 100))
            .arg(qRound(qAbs(steer) * 100)));
    autoDrive_->setText(autopilot ? QStringLiteral("AUTONOMOUS") : QStringLiteral("MANUAL"));
    autoActive_ = autopilot;
    update();
}

void HudOverlayWidget::setDriveModeBadge(const QString& modeLabel)
{
    maneuverHint_->setText(modeLabel);
}

void HudOverlayWidget::updateTwinNavigation(const QString& arrow,
                                            const QString& distanceText,
                                            const double etaSec,
                                            const double routeProgress,
                                            const double speedLimitKmh)
{
    if (!arrow.isEmpty()) {
        maneuverArrows_->setText(arrow);
    }
    if (!distanceText.isEmpty()) {
        maneuverDist_->setText(distanceText);
    }
    if (etaSec > 0.5) {
        maneuverHint_->setText(
            QStringLiteral("%1 · ETA %2s · %3%")
                .arg(maneuverHint_->text().split(QLatin1String(" · ")).value(0, QStringLiteral("TWIN")))
                .arg(qRound(etaSec))
                .arg(qRound(routeProgress * 100.0)));
    }
    if (speedLimitKmh > 1.0) {
        speedLimit_->setText(QString::number(qRound(speedLimitKmh)));
    }
    update();
}

void HudOverlayWidget::updateGearFromPipeline(const double speedMps)
{
    const QString gearIdle = QStringLiteral(
        "color: #64748b; font-size: 15px; font-weight: 700; padding: 4px 8px; border-radius: 6px;");
    const QString gearActive = QStringLiteral(
        "color: #f8fafc; font-size: 15px; font-weight: 800; padding: 4px 8px;"
        "background: rgba(37,99,235,0.65); border: 1px solid rgba(96,165,250,0.7); border-radius: 6px;");
    for (auto* g : {gearP_, gearR_, gearN_, gearD_}) {
        g->setStyleSheet(gearIdle);
    }
    QLabel* active = gearD_;
    if (speedMps < 0.2) {
        active = gearP_;
    } else if (speedMps < 0.0) {
        active = gearR_;
    }
    active->setStyleSheet(gearActive);
}

void HudOverlayWidget::refreshGpsLine()
{
    if (!pipeline_) {
        gpsLine_->setText(QStringLiteral("%1 · GPS —").arg(vehicleId_));
        return;
    }
    const auto& pose = pipeline_->egoViewModel()->pose();
    if (!pose.isValid()) {
        gpsLine_->setText(QStringLiteral("%1 · GPS acquiring…").arg(vehicleId_));
        return;
    }
    gpsLine_->setText(
        QStringLiteral("%1 · GPS %2 , %3 m")
            .arg(vehicleId_)
            .arg(pose.x, 0, 'f', 1)
            .arg(pose.y, 0, 'f', 1));
}

void HudOverlayWidget::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    QLinearGradient bottomGlow(0, r.height() * 0.55, 0, r.height());
    bottomGlow.setColorAt(0.0, QColor(34, 211, 238, 0));
    bottomGlow.setColorAt(1.0, QColor(34, 211, 238, 28));
    p.fillRect(r.adjusted(0, static_cast<int>(r.height() * 0.5), 0, 0), bottomGlow);

    if (speedValue_) {
        const QRect sr = speedValue_->geometry().adjusted(-8, -6, 8, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(56, 189, 248, 35));
        p.drawRoundedRect(sr, 14, 14);
    }
    if (autoDrive_ && autoActive_) {
        const QRect ar = autoDrive_->geometry().adjusted(-6, -4, 6, 4);
        p.setPen(QPen(QColor(74, 222, 128, 90), 1.5));
        p.setBrush(QColor(74, 222, 128, 18));
        p.drawRoundedRect(ar, 12, 12);
    }
    Q_UNUSED(event);
}

}  // namespace sdv::dash::carla
