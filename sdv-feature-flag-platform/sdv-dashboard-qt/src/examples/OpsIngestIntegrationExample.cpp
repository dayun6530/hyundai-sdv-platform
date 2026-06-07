#include "examples/OpsIngestIntegrationExample.h"

#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/PerceptionViewModel.h"
#include "ops/viewmodels/TrajectoryViewModel.h"

#include <QLabel>
#include <QVBoxLayout>

namespace sdv::dash::ops {

OpsIngestIntegrationPanel::OpsIngestIntegrationPanel(
    OpsTelemetryPipeline* pipeline, QWidget* parent)
: QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* title = new QLabel(QStringLiteral("ROS bridge telemetry (Phase 2)"), this);
    title->setStyleSheet(QStringLiteral("color:#8ea6c7;font-size:10px;font-weight:600;"));
    layout->addWidget(title);

    auto* bridgeLab = new QLabel(QStringLiteral("ROS WS: starting…"), this);
    auto* egoLab = new QLabel(QStringLiteral("Ego: —"), this);
    auto* trajLab = new QLabel(QStringLiteral("Trajectory: —"), this);
    auto* percLab = new QLabel(QStringLiteral("Perception: —"), this);

    for (QLabel* lab : {bridgeLab, egoLab, trajLab, percLab}) {
        lab->setWordWrap(true);
        lab->setStyleSheet(QStringLiteral("color:#cbd7e8;font-size:9px;"));
        layout->addWidget(lab);
    }

    if (!pipeline) {
        bridgeLab->setText(QStringLiteral("Pipeline not configured"));
        return;
    }

    connect(pipeline, &OpsTelemetryPipeline::bridgeConnectedChanged, this, [bridgeLab](bool up) {
        bridgeLab->setText(up ? QStringLiteral("ROS WS: connected")
                            : QStringLiteral("ROS WS: reconnecting"));
        bridgeLab->setStyleSheet(up ? QStringLiteral("color:#34d399;font-size:9px;")
                                    : QStringLiteral("color:#f97316;font-size:9px;"));
    });
    bridgeLab->setText(pipeline->isBridgeConnected() ? QStringLiteral("ROS WS: connected")
                                                     : QStringLiteral("ROS WS: reconnecting"));

    connect(pipeline->egoViewModel(), &EgoViewModel::changed, this, [egoLab, pipeline]() {
        egoLab->setText(pipeline->egoViewModel()->summary());
    });
    egoLab->setText(pipeline->egoViewModel()->summary());

    connect(pipeline->trajectoryViewModel(), &TrajectoryViewModel::changed, this, [trajLab, pipeline]() {
        trajLab->setText(pipeline->trajectoryViewModel()->summary());
    });
    trajLab->setText(pipeline->trajectoryViewModel()->summary());

    connect(pipeline->perceptionViewModel(), &PerceptionViewModel::changed, this, [percLab, pipeline]() {
        percLab->setText(pipeline->perceptionViewModel()->summary());
    });
    percLab->setText(pipeline->perceptionViewModel()->summary());
}

}  // namespace sdv::dash::ops
