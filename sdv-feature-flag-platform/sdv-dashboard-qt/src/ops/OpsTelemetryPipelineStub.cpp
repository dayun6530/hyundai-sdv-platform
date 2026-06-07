#include "ops/OpsTelemetryPipeline.h"

#include "ops/ingest/SignalManager.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/PerceptionViewModel.h"
#include "ops/viewmodels/TrajectoryViewModel.h"

namespace sdv::dash::ops {

OpsTelemetryPipeline::OpsTelemetryPipeline(QObject* parent) : QObject(parent)
{
    signalManager_ = new SignalManager(this);
    egoViewModel_ = new EgoViewModel(this);
    trajectoryViewModel_ = new TrajectoryViewModel(this);
    perceptionViewModel_ = new PerceptionViewModel(this);
    emit statusLineChanged(QStringLiteral(
        "ROS WS: unavailable (install Qt 6 WebSockets module, then rebuild)"));
}

OpsTelemetryPipeline::~OpsTelemetryPipeline() = default;

void OpsTelemetryPipeline::start(const QUrl&)
{
    emit statusLineChanged(QStringLiteral(
        "ROS WS: unavailable — Qt WebSockets not installed at build time"));
}

void OpsTelemetryPipeline::stop() {}

}  // namespace sdv::dash::ops
