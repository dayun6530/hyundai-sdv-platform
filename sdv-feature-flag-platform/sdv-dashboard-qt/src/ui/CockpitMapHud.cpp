#include "ui/CockpitMapHud.h"

#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>

#include <QtMath>

namespace sdv::dash::ui {

namespace {

void styleLabel(QLabel* label, const QString& css)
{
    label->setStyleSheet(css);
    label->setAttribute(Qt::WA_TranslucentBackground, true);
}

void drawArc(QPainter& p, const QRectF& rect, const double fill01, const QColor& color)
{
    p.save();
    p.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const int span = static_cast<int>(-240 * 16 * qBound(0.08, fill01, 1.0));
    p.drawArc(rect, 210 * 16, span);
    p.restore();
}

}  // namespace

CockpitMapHud::CockpitMapHud(QWidget* parent) : QFrame(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setStyleSheet(QStringLiteral("background: transparent; border: 0;"));

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(20, 64, 20, 24);
    grid->setHorizontalSpacing(8);

    auto* left = new QVBoxLayout();
    speedValue_ = new QLabel(QStringLiteral("0"), this);
    styleLabel(speedValue_, QStringLiteral(
        "color: #f8fafc; font-size: 48px; font-weight: 800; letter-spacing: -1px;"));
    auto* speedRow = new QHBoxLayout();
    speedRow->setSpacing(6);
    speedRow->addWidget(speedValue_);
    auto* km = new QLabel(QStringLiteral("km"), this);
    styleLabel(km, QStringLiteral("color: #cbd5e1; font-size: 14px; font-weight: 600;"));
    speedRow->addWidget(km, 0, Qt::AlignBottom);
    speedLimit_ = new QLabel(QStringLiteral("80"), this);
    speedLimit_->setFixedSize(36, 36);
    speedLimit_->setAlignment(Qt::AlignCenter);
    speedLimit_->setStyleSheet(QStringLiteral(
        "color: #fecaca; font-weight: 700; font-size: 12px;"
        "background: rgba(40,8,8,0.55); border: 2px solid #ef4444; border-radius: 18px;"));
    speedRow->addWidget(speedLimit_, 0, Qt::AlignBottom);
    speedRow->addStretch();
    left->addLayout(speedRow);
    left->addStretch();
    grid->addLayout(left, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->setColumnStretch(1, 1);

    auto* bottom = new QGridLayout();

    auto* nav = new QFrame(this);
    nav->setStyleSheet(QStringLiteral(
        "background: rgba(15,23,42,0.78); border-radius: 10px;"
        "border: 1px solid rgba(71,85,105,0.55);"));
    auto* navLay = new QHBoxLayout(nav);
    navLay->setContentsMargins(12, 7, 12, 7);
    maneuverArrows_ = new QLabel(QStringLiteral("↑  ↑  ↑  ↑"), nav);
    styleLabel(maneuverArrows_, QStringLiteral(
        "color: #f8fafc; font-size: 15px; font-weight: 700; letter-spacing: 2px;"));
    maneuverDist_ = new QLabel(QStringLiteral("1.4 km"), nav);
    styleLabel(maneuverDist_, QStringLiteral("color: #f8fafc; font-size: 14px; font-weight: 700;"));
    navLay->addWidget(maneuverArrows_);
    navLay->addWidget(maneuverDist_);
    bottom->addWidget(nav, 0, 1, Qt::AlignHCenter | Qt::AlignBottom);

    auto* gearCol = new QVBoxLayout();
    odo_ = new QLabel(QStringLiteral("2659 km"), this);
    styleLabel(odo_, QStringLiteral("color: #94a3b8; font-size: 11px;"));
    gearD_ = new QLabel(QStringLiteral("P  R  N  D"), this);
    styleLabel(gearD_, QStringLiteral("color: #94a3b8; font-size: 12px; font-weight: 700;"));
    gearCol->addWidget(odo_, 0, Qt::AlignRight);
    gearCol->addWidget(gearD_, 0, Qt::AlignRight);
    bottom->addLayout(gearCol, 0, 2, Qt::AlignRight | Qt::AlignBottom);

    grid->addLayout(bottom, 1, 0, 1, 3);
}

void CockpitMapHud::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int h = height();
    const QRectF leftArc(8, h * 0.2, 110, 190);
    drawArc(p, leftArc, qBound(0.0, speedKmh_ / 120.0, 1.0), QColor(148, 163, 184, 170));

    // Dashboard sill (in-cabin lower edge)
    QLinearGradient dash(0, h - 42, 0, h);
    dash.setColorAt(0, QColor(3, 7, 17, 0));
    dash.setColorAt(0.45, QColor(3, 7, 17, 140));
    dash.setColorAt(1, QColor(3, 7, 17, 235));
    p.fillRect(0, h - 42, width(), 42, dash);
}

void CockpitMapHud::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(pipeline_->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, &CockpitMapHud::onEgoChanged);
    onEgoChanged();
}

void CockpitMapHud::onEgoChanged()
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
    update();
}

void CockpitMapHud::updateNavGuidance(const QString& maneuverArrows, const QString& distanceText)
{
    if (!maneuverArrows.isEmpty()) {
        maneuverArrows_->setText(maneuverArrows);
    }
    if (!distanceText.isEmpty()) {
        maneuverDist_->setText(distanceText);
    }
}

void CockpitMapHud::updateFromCarState(const QJsonObject& /*carState*/, const bool /*mqttConnected*/)
{
    if (pipeline_) {
        onEgoChanged();
    } else {
        update();
    }
}

}  // namespace sdv::dash::ui
