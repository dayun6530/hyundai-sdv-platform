#include "ui/CockpitHudOverlay.h"

#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ui/DashboardTheme.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace sdv::dash::ui {

namespace {

QFrame* chip(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setObjectName(QStringLiteral("hudChip"));
    f->setStyleSheet(DashboardTheme::hudChipStyle());
    return f;
}

}  // namespace

CockpitHudOverlay::CockpitHudOverlay(QWidget* parent) : QFrame(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setStyleSheet(QStringLiteral("background: transparent; border: 0;"));

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(14, 12, 14, 12);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    auto* topLeft = chip(this);
    auto* tlLay = new QHBoxLayout(topLeft);
    tlLay->setContentsMargins(10, 8, 10, 8);
    vehicleCombo_ = new QComboBox(topLeft);
    for (int i = 1; i <= 10; ++i) {
        vehicleCombo_->addItem(QStringLiteral("car_%1").arg(i));
    }
    mqttDot_ = new QLabel(QStringLiteral("●"), topLeft);
    mqttDot_->setStyleSheet(QStringLiteral("color: #34d399; font-size: 11px;"));
    mqttText_ = new QLabel(QStringLiteral("MQTT Connected"), topLeft);
    mqttText_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 10px;"));
    tlLay->addWidget(vehicleCombo_, 1);
    tlLay->addWidget(mqttDot_);
    tlLay->addWidget(mqttText_);
    grid->addWidget(topLeft, 0, 0, Qt::AlignLeft | Qt::AlignTop);

    autoDrive_ = new QLabel(QStringLiteral("AUTO DRIVE ACTIVE"), this);
    autoDrive_->setStyleSheet(QStringLiteral(
        "color: #4ade80; font-size: 13px; font-weight: 800; letter-spacing: 1px;"
        "background: rgba(4,16,10,0.55); border: 1px solid rgba(74,222,128,0.45);"
        "border-radius: 10px; padding: 8px 14px;"));
    grid->addWidget(autoDrive_, 0, 1, Qt::AlignRight | Qt::AlignTop);

    connect(vehicleCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &CockpitHudOverlay::onVehicleIndexChanged);

    speedValue_ = nullptr;
    speedLimit_ = nullptr;
    adasState_ = nullptr;
    routeLine_ = nullptr;
    maneuverDist_ = nullptr;
    maneuverHint_ = nullptr;
    gearP_ = gearR_ = gearN_ = gearD_ = nullptr;
    evSoc_ = evRange_ = evTemp_ = odo_ = nullptr;
}

void CockpitHudOverlay::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    Q_UNUSED(pipeline_);
}

void CockpitHudOverlay::onEgoChanged()
{
}

void CockpitHudOverlay::updateFromCarState(const QJsonObject& carState, const bool mqttConnected)
{
    setMqttStatus(mqttConnected);

    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
    const bool adasOn = adas.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0 ||
                        !adas.value(QStringLiteral("active_version")).toString().isEmpty();
    autoDrive_->setVisible(true);
    autoDrive_->setText(adasOn ? QStringLiteral("AUTO DRIVE ACTIVE") : QStringLiteral("MANUAL MODE"));
}

void CockpitHudOverlay::setMqttStatus(const bool connected)
{
    mqttDot_->setStyleSheet(connected ? QStringLiteral("color: #4ade80; font-size: 11px;")
                                      : QStringLiteral("color: #f97316; font-size: 11px;"));
    mqttText_->setText(connected ? QStringLiteral("MQTT Connected") : QStringLiteral("MQTT Offline"));
}

void CockpitHudOverlay::onVehicleIndexChanged(const int index)
{
    if (index < 0) {
        return;
    }
    emit vehicleSelected(vehicleCombo_->itemText(index));
}

}  // namespace sdv::dash::ui
