#include "carla/control/CarlaControlPanel.h"

#include "carla/control/CarlaControlManager.h"
#include "carla/control/CarlaVehicleController.h"
#include "ui/DashboardTheme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDoubleSpinBox>

using sdv::dash::ui::DashboardTheme;

namespace sdv::dash::carla::control {

namespace {

QLabel* chipLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(DashboardTheme::hudChipStyle());
    return l;
}

}  // namespace

CarlaControlPanel::CarlaControlPanel(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(QStringLiteral("background: transparent;"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 8, 12, 8);
    outer->setSpacing(6);

    auto* row1 = new QHBoxLayout();
    row1->setSpacing(8);

    modeBadge_ = new QLabel(QStringLiteral("MANUAL"), this);
    modeBadge_->setStyleSheet(QStringLiteral(
        "color: #f8fafc; font-size: 11px; font-weight: 900; letter-spacing: 1.5px;"
        "background: rgba(8,20,40,0.92); border: 1px solid rgba(56,189,248,0.6);"
        "border-radius: 8px; padding: 6px 10px;"));
    row1->addWidget(modeBadge_);

    destHint_ = chipLabel(QStringLiteral("DEST: Shift+click minimap"), this);
    row1->addWidget(destHint_);

    agentStatus_ = chipLabel(QStringLiteral("AGENT idle"), this);
    row1->addWidget(agentStatus_);

    row1->addStretch();
    outer->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    row2->setSpacing(8);

    agentModeBox_ = new QComboBox(this);
    agentModeBox_->addItems({QStringLiteral("behavior"), QStringLiteral("basic")});
    agentModeBox_->setStyleSheet(DashboardTheme::hudChipStyle());
    row2->addWidget(agentModeBox_);

    cameraBox_ = new QComboBox(this);
    cameraBox_->addItems(
        {QStringLiteral("Front"), QStringLiteral("Rear"), QStringLiteral("Left"), QStringLiteral("Right")});
    cameraBox_->setStyleSheet(DashboardTheme::hudChipStyle());
    connect(cameraBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CarlaControlPanel::onCameraChanged);
    row2->addWidget(cameraBox_);

    autopilotBtn_ = new QPushButton(QStringLiteral("AUTOPILOT"), this);
    autopilotBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { color: #4ade80; font-weight: 800; padding: 8px 14px;"
        "background: rgba(4,30,18,0.9); border: 1px solid rgba(74,222,128,0.65); border-radius: 8px; }"
        "QPushButton:checked { background: rgba(74,222,128,0.35); }"));
    autopilotBtn_->setCheckable(true);
    connect(autopilotBtn_, &QPushButton::clicked, this, &CarlaControlPanel::onAutopilotClicked);
    row2->addWidget(autopilotBtn_);

    startAutoBtn_ = new QPushButton(QStringLiteral("START AUTO"), this);
    startAutoBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { color: #67e8f9; font-weight: 800; padding: 8px 12px;"
        "background: rgba(8,30,50,0.9); border: 1px solid rgba(34,211,238,0.7); border-radius: 8px; }"
        "QPushButton:disabled { color: #64748b; border-color: #475569; }"));
    connect(startAutoBtn_, &QPushButton::clicked, this, &CarlaControlPanel::onStartAutoClicked);
    row2->addWidget(startAutoBtn_);

    auto* pauseBtn = new QPushButton(QStringLiteral("PAUSE"), this);
    pauseBtn->setStyleSheet(DashboardTheme::hudChipStyle());
    connect(pauseBtn, &QPushButton::clicked, this, [this]() {
        if (manager_) {
            manager_->pauseAutonomousDriving();
        }
    });
    row2->addWidget(pauseBtn);

    auto* resumeBtn = new QPushButton(QStringLiteral("RESUME"), this);
    resumeBtn->setStyleSheet(DashboardTheme::hudChipStyle());
    connect(resumeBtn, &QPushButton::clicked, this, [this]() {
        if (manager_) {
            manager_->resumeAutonomousDriving();
        }
    });
    row2->addWidget(resumeBtn);

    emergencyBtn_ = new QPushButton(QStringLiteral("E-STOP"), this);
    emergencyBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { color: #fecaca; font-weight: 900; padding: 8px 14px;"
        "background: rgba(60,8,8,0.92); border: 2px solid #ef4444; border-radius: 8px; }"));
    connect(emergencyBtn_, &QPushButton::clicked, this, &CarlaControlPanel::onEmergencyClicked);
    row2->addWidget(emergencyBtn_);

    destX_ = new QDoubleSpinBox(this);
    destX_->setRange(-5000, 5000);
    destX_->setDecimals(1);
    destX_->setPrefix(QStringLiteral("X "));
    destY_ = new QDoubleSpinBox(this);
    destY_->setRange(-5000, 5000);
    destY_->setDecimals(1);
    destY_->setPrefix(QStringLiteral("Y "));
    for (auto* spin : {destX_, destY_}) {
        spin->setStyleSheet(DashboardTheme::hudChipStyle());
        row2->addWidget(spin);
    }

    auto* routeBtn = new QPushButton(QStringLiteral("ROUTE"), this);
    routeBtn->setStyleSheet(DashboardTheme::hudChipStyle());
    connect(routeBtn, &QPushButton::clicked, this, &CarlaControlPanel::onRouteClicked);
    row2->addWidget(routeBtn);

    row2->addStretch();
    outer->addLayout(row2);
}

void CarlaControlPanel::bindManager(CarlaControlManager* manager)
{
    manager_ = manager;
    if (!manager_) {
        return;
    }
    connect(manager_, &CarlaControlManager::driveModeChanged, this, [this](const QString& label) {
        modeBadge_->setText(label);
        const bool autoOn = label.contains(QStringLiteral("AUTONOMOUS"), Qt::CaseInsensitive);
        autopilotBtn_->setChecked(autoOn);
        modeBadge_->setStyleSheet(autoOn
            ? QStringLiteral(
                  "color: #4ade80; font-size: 11px; font-weight: 900; letter-spacing: 1.5px;"
                  "background: rgba(4,30,18,0.92); border: 1px solid rgba(74,222,128,0.7);"
                  "border-radius: 8px; padding: 6px 10px;")
            : QStringLiteral(
                  "color: #f8fafc; font-size: 11px; font-weight: 900; letter-spacing: 1.5px;"
                  "background: rgba(8,20,40,0.92); border: 1px solid rgba(56,189,248,0.6);"
                  "border-radius: 8px; padding: 6px 10px;"));
    });
    connect(manager_, &CarlaControlManager::destinationHintChanged, destHint_, &QLabel::setText);
    connect(manager_, &CarlaControlManager::agentStatusChanged, this,
            [this](const QString& mode, const QString& state, const QString& maneuver) {
                agentStatus_->setText(QStringLiteral("AGENT %1 · %2 · %3").arg(mode, state, maneuver));
                refreshAgentUi();
            });
    refreshAgentUi();
}

void CarlaControlPanel::refreshAgentUi()
{
    if (!manager_ || !startAutoBtn_) {
        return;
    }
    startAutoBtn_->setEnabled(true);
    startAutoBtn_->setToolTip(QStringLiteral("Requires destination or live ego pose for default goal"));
}

void CarlaControlPanel::onStartAutoClicked()
{
    if (!manager_) {
        return;
    }
    const QString mode = agentModeBox_ ? agentModeBox_->currentText() : QStringLiteral("behavior");
    if (!manager_->startAutonomousDriving(mode)) {
        agentStatus_->setText(QStringLiteral("AGENT blocked — set DEST (Shift+click minimap)"));
    }
}

void CarlaControlPanel::onCameraChanged(const int index)
{
    if (!manager_) {
        return;
    }
    using Slot = CarlaVehicleController::CameraSlot;
    Slot slot = Slot::Front;
    switch (index) {
    case 1:
        slot = Slot::Rear;
        break;
    case 2:
        slot = Slot::Left;
        break;
    case 3:
        slot = Slot::Right;
        break;
    default:
        break;
    }
    manager_->setCameraSlot(slot);
}

void CarlaControlPanel::onAutopilotClicked()
{
    if (manager_) {
        manager_->toggleAutopilot();
    }
}

void CarlaControlPanel::onEmergencyClicked()
{
    if (manager_) {
        manager_->triggerEmergencyStop();
    }
}

void CarlaControlPanel::onRouteClicked()
{
    if (!manager_) {
        return;
    }
    manager_->setDestination(destX_->value(), destY_->value());
}

void CarlaControlPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRect r = rect().adjusted(4, 2, -4, -2);
    p.setPen(QPen(QColor(56, 189, 248, 80), 1.0));
    p.setBrush(QColor(8, 16, 32, 200));
    p.drawRoundedRect(r, 12, 12);
}

}  // namespace sdv::dash::carla::control
