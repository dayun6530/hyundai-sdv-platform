#include "ops/OpsTelemetryPipeline.h"

#include <QLoggingCategory>

#include "ops/ingest/RosBridgeClient.h"

Q_LOGGING_CATEGORY(sdvOpsTelemetry, "sdv.ops.telemetry")
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

    bridgeClient_ = new RosBridgeClient();
    bridgeClient_->moveToThread(&workerThread_);

    connect(&workerThread_, &QThread::finished, bridgeClient_, &QObject::deleteLater);

    statusEmitTimer_.setInterval(400);
    statusEmitTimer_.setSingleShot(true);
    connect(&statusEmitTimer_, &QTimer::timeout, this, &OpsTelemetryPipeline::emitStatusLine);

    connect(bridgeClient_, &RosBridgeClient::connected, this, [this]() {
        setBridgeConnected(true);
        scheduleStatusEmit();
    }, Qt::QueuedConnection);

    connect(bridgeClient_, &RosBridgeClient::disconnected, this, [this]() {
        setBridgeConnected(false);
        scheduleStatusEmit();
    }, Qt::QueuedConnection);

    connect(bridgeClient_, &RosBridgeClient::connectionError, this, [this](const QString& message) {
        emit statusLineChanged(QStringLiteral("ROS bridge error: %1").arg(message));
    }, Qt::QueuedConnection);

    connect(
        bridgeClient_, &RosBridgeClient::jsonFrameReceived,
        signalManager_, &SignalManager::ingestJsonFrame,
        Qt::QueuedConnection);

    connect(
        bridgeClient_, &RosBridgeClient::cameraJsonReceived, this,
        &OpsTelemetryPipeline::frontCameraJsonReceived, Qt::QueuedConnection);

    connect(
        signalManager_, &SignalManager::bridgeHelloReceived,
        this, [this]() {
            emit statusLineChanged(QStringLiteral("ROS bridge ready (telemetry stream)"));
        });

    connect(
        signalManager_, &SignalManager::egoPoseUpdated,
        egoViewModel_, &EgoViewModel::apply);
    connect(
        signalManager_, &SignalManager::trajectoryUpdated,
        trajectoryViewModel_, &TrajectoryViewModel::apply);
    connect(
        signalManager_, &SignalManager::perceptionUpdated,
        perceptionViewModel_, &PerceptionViewModel::apply);

    connect(egoViewModel_, &EgoViewModel::changed, this, &OpsTelemetryPipeline::scheduleStatusEmit);
    connect(trajectoryViewModel_, &TrajectoryViewModel::changed, this, &OpsTelemetryPipeline::scheduleStatusEmit);
    connect(perceptionViewModel_, &PerceptionViewModel::changed, this, &OpsTelemetryPipeline::scheduleStatusEmit);

    workerThread_.setObjectName(QStringLiteral("SdvRosBridgeWsThread"));
    workerThread_.start();
}

OpsTelemetryPipeline::~OpsTelemetryPipeline()
{
    stop();
    workerThread_.quit();
    workerThread_.wait(3000);
}

void OpsTelemetryPipeline::start(const QUrl& url)
{
    QMetaObject::invokeMethod(
        bridgeClient_, "start", Qt::QueuedConnection, Q_ARG(QUrl, url));
    emit statusLineChanged(QStringLiteral("Connecting to %1 …").arg(url.toString()));
}

void OpsTelemetryPipeline::stop()
{
    QMetaObject::invokeMethod(bridgeClient_, "stop", Qt::QueuedConnection);
    setBridgeConnected(false);
}

void OpsTelemetryPipeline::setBridgeConnected(const bool connected)
{
    if (bridgeConnected_ == connected) {
        return;
    }
    bridgeConnected_ = connected;
    emit bridgeConnectedChanged(connected);
}

void OpsTelemetryPipeline::scheduleStatusEmit()
{
    if (!statusEmitTimer_.isActive()) {
        statusEmitTimer_.start();
    }
}

void OpsTelemetryPipeline::emitStatusLine()
{
    const QString bridge =
        bridgeConnected_ ? QStringLiteral("ROS WS: connected") : QStringLiteral("ROS WS: reconnecting");

    const QString line = QStringLiteral("%1 | %2 | %3 | %4")
                             .arg(bridge,
                                 egoViewModel_->summary(),
                                 trajectoryViewModel_->summary(),
                                 perceptionViewModel_->summary());
    emit statusLineChanged(line);
    qCInfo(sdvOpsTelemetry).noquote() << line;
}

}  // namespace sdv::dash::ops
