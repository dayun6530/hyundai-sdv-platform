#include "OTADeploymentWidget.h"

#include "modules/ota/OtaDeploymentService.h"
#include "modules/ota/OtaDeploymentViewModel.h"
#include "modules/ota/OtaTypes.h"
#include "ui/DashboardTheme.h"
#include "ui/OtaFlowDiagramWidget.h"
#include "ui/UiAnimation.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QFont>
#include <QVBoxLayout>

using sdv::dash::OTADeploymentWidget;
using sdv::dash::ui::DashboardTheme;
using sdv::dash::ui::UiAnimation;

OTADeploymentWidget::OTADeploymentWidget(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);

    setStyleSheet(DashboardTheme::otaPanelStyle());

    auto* selectorRow = new QHBoxLayout();
    auto* targetLbl = new QLabel(QStringLiteral("Target version"), this);
    targetLbl->setObjectName(QStringLiteral("otaMeta"));
    selectorRow->addWidget(targetLbl);
    versionSelector_ = new QComboBox(this);
    versionSelector_->setMinimumWidth(120);
    selectorRow->addWidget(versionSelector_, 1);
    deployBtn_ = new QPushButton(QStringLiteral("Deploy"), this);
    deployBtn_->setStyleSheet(DashboardTheme::primaryButtonStyle());
    connect(deployBtn_, &QPushButton::clicked, this, &OTADeploymentWidget::onDeploy);
    selectorRow->addWidget(deployBtn_);
    v->addLayout(selectorRow);

    auto* g = new QGridLayout();
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(3);
    auto mkMeta = [this](const QString& t) {
        auto* l = new QLabel(t, this);
        l->setObjectName(QStringLiteral("otaMeta"));
        return l;
    };
    g->addWidget(mkMeta(QStringLiteral("Current")), 0, 0);
    activeVer_ = new QLabel(QStringLiteral("—"), this);
    activeVer_->setObjectName(QStringLiteral("otaValue"));
    g->addWidget(activeVer_, 0, 1);
    g->addWidget(mkMeta(QStringLiteral("Target")), 1, 0);
    targetVer_ = new QLabel(QStringLiteral("—"), this);
    targetVer_->setObjectName(QStringLiteral("otaValue"));
    g->addWidget(targetVer_, 1, 1);
    g->addWidget(mkMeta(QStringLiteral("State")), 2, 0);
    stateLabel_ = new QLabel(QStringLiteral("Idle"), this);
    stateLabel_->setObjectName(QStringLiteral("otaValue"));
    g->addWidget(stateLabel_, 2, 1);
    v->addLayout(g);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setTextVisible(true);
    progress_->setFormat(QStringLiteral("%p% — %v"));
    progress_->setFixedHeight(16);
    progress_->setStyleSheet(DashboardTheme::progressBarStyle(QStringLiteral("#22d3ee")));
    v->addWidget(progress_);

    flowDiagram_ = new sdv::dash::ui::OtaFlowDiagramWidget(this);
    v->addWidget(flowDiagram_);

    deploymentLog_ = new QTextEdit(this);
    deploymentLog_->setReadOnly(true);
    deploymentLog_->setMaximumHeight(88);
    deploymentLog_->setFont(QFont(QStringLiteral("Consolas"), 8));
    v->addWidget(deploymentLog_);

    auto* row = new QHBoxLayout();
    rollback_ = new QPushButton(QStringLiteral("Rollback"), this);
    rollback_->setStyleSheet(DashboardTheme::ghostButtonStyle());
    connect(rollback_, &QPushButton::clicked, this, &OTADeploymentWidget::onRollback);
    row->addStretch();
    row->addWidget(rollback_);
    v->addLayout(row);

    appendLog(QStringLiteral("[OTA] OTA deployment module ready"));
    UiAnimation::fadeIn(this);
}

void OTADeploymentWidget::setViewModel(sdv::dash::ota::OtaDeploymentViewModel* viewModel)
{
    if (viewModel_ == viewModel) {
        return;
    }
    if (viewModel_) {
        disconnect(viewModel_, nullptr, this, nullptr);
    }
    viewModel_ = viewModel;
    if (!viewModel_) {
        return;
    }

    versionSelector_->clear();
    versionSelector_->addItems(viewModel_->availableVersions());

    connect(viewModel_, &sdv::dash::ota::OtaDeploymentViewModel::changed, this, &OTADeploymentWidget::onViewModelChanged);
    connect(viewModel_, &sdv::dash::ota::OtaDeploymentViewModel::logLine, this, &OTADeploymentWidget::onLogLine);
    onViewModelChanged();
}

void OTADeploymentWidget::setApiBase(const QString& url)
{
    if (viewModel_ && viewModel_->service()) {
        viewModel_->service()->setApiBase(url);
    }
}

void OTADeploymentWidget::setCarId(const QString& carId)
{
    if (viewModel_ && viewModel_->service()) {
        viewModel_->service()->setCarId(carId);
    }
}

void OTADeploymentWidget::updateFromCarState(const QJsonObject& carState)
{
    if (viewModel_) {
        viewModel_->ingestCarState(carState);
    }
}

void OTADeploymentWidget::onDeploy()
{
    if (!viewModel_) {
        return;
    }
    const QString ver = versionSelector_->currentText().trimmed();
    if (ver.isEmpty()) {
        return;
    }
    if (ver == viewModel_->activeVersion()) {
        QMessageBox::information(this, QStringLiteral("Deploy"), QStringLiteral("Already running %1").arg(ver));
        return;
    }
    viewModel_->deploySelectedVersion(ver);
}

void OTADeploymentWidget::onRollback()
{
    if (!viewModel_) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Rollback"),
        QStringLiteral("Rollback ADAS to previous version on this vehicle?"));
    if (answer != QMessageBox::Yes) {
        return;
    }
    viewModel_->requestRollback();
}

void OTADeploymentWidget::onViewModelChanged()
{
    if (!viewModel_) {
        return;
    }

    activeVer_->setText(viewModel_->activeVersion().isEmpty() ? QStringLiteral("—") : viewModel_->activeVersion());
    const QString target = viewModel_->targetVersion();
    targetVer_->setText(target.isEmpty() ? QStringLiteral("—") : target);
    stateLabel_->setText(viewModel_->currentState());
    UiAnimation::animateProgress(progress_, viewModel_->progress());
    updateProgressStyle();

    const bool busy = viewModel_->busy();
    deployBtn_->setEnabled(!busy);
    rollback_->setEnabled(!busy);

    if (flowDiagram_) {
        flowDiagram_->setDeploymentState(viewModel_->currentState());
    }

    if (lastState_ != viewModel_->currentState()) {
        lastState_ = viewModel_->currentState();
    }
}

void OTADeploymentWidget::updateProgressStyle()
{
    if (!viewModel_) {
        return;
    }
    const QString state = viewModel_->currentState();
    if (state == QStringLiteral("Failed")) {
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#b91c1c; }"));
    } else if (state == QStringLiteral("Completed")) {
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#15803d; }"));
    } else if (state == QStringLiteral("Rollback")) {
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#d97706; }"));
    } else {
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#2563eb; }"));
    }
    progress_->setFormat(QStringLiteral("%1 — %p%").arg(state));
}

void OTADeploymentWidget::rebuildTimeline()
{
    if (flowDiagram_ && viewModel_) {
        flowDiagram_->setDeploymentState(viewModel_->currentState());
    }
}

void OTADeploymentWidget::appendLog(const QString& line)
{
    deploymentLog_->append(line);
    if (deploymentLog_->document()->blockCount() > 120) {
        deploymentLog_->clear();
        deploymentLog_->append(QStringLiteral("[OTA] (log trimmed)"));
    }
}

void OTADeploymentWidget::onLogLine(const QString& line)
{
    appendLog(line);
}
