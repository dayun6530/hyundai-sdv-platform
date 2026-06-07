#include "carla/integration/CarlaIntegrationController.h"

#include "carla/CarlaCockpitView.h"
#include "carla/CarlaCameraReceiver.h"
#include "carla/integration/CarlaDataBridge.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/ingest/SignalManager.h"

namespace sdv::dash::carla::integration {

CarlaIntegrationController::CarlaIntegrationController(QObject* parent) : QObject(parent)
{
    bridge_ = new CarlaDataBridge(this);
    connect(bridge_, &CarlaDataBridge::statusMessage, this, &CarlaIntegrationController::statusMessage);
}

CarlaIntegrationController::~CarlaIntegrationController()
{
    stop();
}

HudViewModel* CarlaIntegrationController::hudViewModel() const
{
    return bridge_ ? bridge_->hudViewModel() : nullptr;
}

void CarlaIntegrationController::configure(
    const Mode mode,
    sdv::dash::ops::OpsTelemetryPipeline* pipeline,
    sdv::dash::carla::CarlaCockpitView* cockpit,
    const QUrl& carlaAdapterWs,
    const quint16 publishPort)
{
    stop();
    mode_ = mode;
    pipeline_ = pipeline;
    cockpit_ = cockpit;

    if (mode == Mode::Off || mode == Mode::ExternalWsBridge) {
        return;
    }

    if (mode == Mode::NativeCarlaWs) {
        bridge_->setAdapterKind(CarlaDataBridge::AdapterKind::WebSocket);
        bridge_->setCarlaWebSocketUrl(carlaAdapterWs);
    } else {
        bridge_->setAdapterKind(CarlaDataBridge::AdapterKind::Simulator);
    }

    bridge_->setPublishPort(publishPort);
    bridge_->setPublishToWebSocket(true);
    bridge_->setIngestToPipeline(pipeline_ != nullptr);

    if (pipeline_ && pipeline_->signalManager()) {
        connect(
            bridge_,
            &CarlaDataBridge::telemetryFrameReady,
            pipeline_->signalManager(),
            &sdv::dash::ops::SignalManager::ingestJsonFrame,
            Qt::QueuedConnection);
    }

    if (cockpit_) {
        connect(
            bridge_,
            &CarlaDataBridge::cameraFrameReady,
            cockpit_,
            &sdv::dash::carla::CarlaCockpitView::ingestCameraFrame,
            Qt::QueuedConnection);
    }
}

void CarlaIntegrationController::start()
{
    if (mode_ == Mode::Off || mode_ == Mode::ExternalWsBridge) {
        return;
    }
    bridge_->start();
    emit statusMessage(QStringLiteral("CARLA integration v1 active"));
}

void CarlaIntegrationController::stop()
{
    if (bridge_) {
        bridge_->stop();
    }
}

}  // namespace sdv::dash::carla::integration
