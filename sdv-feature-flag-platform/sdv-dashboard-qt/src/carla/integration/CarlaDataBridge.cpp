#include "carla/integration/CarlaDataBridge.h"

#include "carla/integration/CarlaSimAdapter.h"
#include "carla/integration/CarlaTelemetrySerializer.h"
#include "carla/integration/CarlaWebSocketAdapter.h"
#include "carla/integration/CarlaWsPublisher.h"
#include "carla/integration/LaneExtractor.h"
#include "carla/integration/PerceptionExtractor.h"
#include "twin/TwinSnapshotParser.h"

namespace sdv::dash::carla::integration {

CarlaDataBridge::CarlaDataBridge(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<CarlaWorldSnapshot>("sdv::dash::carla::integration::CarlaWorldSnapshot");

    hudViewModel_ = new HudViewModel(this);
    publisher_ = new CarlaWsPublisher(this);
    setAdapterKind(AdapterKind::Simulator);

    connect(publisher_, &CarlaWsPublisher::statusMessage, this, &CarlaDataBridge::statusMessage);
}

CarlaDataBridge::~CarlaDataBridge()
{
    stop();
}

void CarlaDataBridge::setAdapterKind(const AdapterKind kind)
{
    if (adapter_) {
        adapter_->stop();
        adapter_->disconnect(this);
        adapter_->deleteLater();
        adapter_ = nullptr;
    }
    adapterKind_ = kind;
    if (kind == AdapterKind::WebSocket) {
        auto* ws = new CarlaWebSocketAdapter(this);
        ws->setEndpoint(wsAdapterUrl_);
        adapter_ = ws;
    } else {
        adapter_ = new CarlaSimAdapter(this);
    }
    connect(adapter_, &CarlaApiAdapter::snapshotReady, this, &CarlaDataBridge::onSnapshot);
    connect(adapter_, &CarlaApiAdapter::statusMessage, this, &CarlaDataBridge::statusMessage);
}

void CarlaDataBridge::setCarlaWebSocketUrl(const QUrl& url)
{
    wsAdapterUrl_ = url;
    if (auto* ws = qobject_cast<CarlaWebSocketAdapter*>(adapter_)) {
        ws->setEndpoint(url);
    }
}

void CarlaDataBridge::setPublishPort(const quint16 port)
{
    publishPort_ = port;
}

void CarlaDataBridge::setPublishToWebSocket(const bool enabled)
{
    publishWs_ = enabled;
}

void CarlaDataBridge::setIngestToPipeline(const bool enabled)
{
    ingestPipeline_ = enabled;
}

void CarlaDataBridge::start()
{
    helloSent_ = false;
    if (publishWs_) {
        publisher_->start(publishPort_);
    }
    if (adapter_) {
        adapter_->start();
    }
    emit statusMessage(QStringLiteral("CARLA data bridge started"));
}

void CarlaDataBridge::stop()
{
    if (adapter_) {
        adapter_->stop();
    }
    publisher_->stop();
    helloSent_ = false;
    emit statusMessage(QStringLiteral("CARLA data bridge stopped"));
}

bool CarlaDataBridge::isRunning() const
{
    return adapter_ && adapter_->isRunning();
}

void CarlaDataBridge::onSnapshot(const CarlaWorldSnapshot& snapshotIn)
{
    CarlaWorldSnapshot snapshot = snapshotIn;
    sdv::dash::twin::TwinSnapshotParser::mergeTrafficLightsIntoSnapshot(snapshot, snapshot.twin);

    hudViewModel_->applySnapshot(snapshot);

    const auto perception = PerceptionExtractor::extract(snapshot);
    const auto laneTrajectory = LaneExtractor::extract(snapshot);
    const QVector<QByteArray> frames =
        CarlaTelemetrySerializer::snapshotToTelemetryFrames(snapshot, laneTrajectory, perception);

    if (publishWs_ && publisher_->isListening()) {
        if (!helloSent_) {
            publisher_->publishFrame(CarlaTelemetrySerializer::bridgeHello());
            helloSent_ = true;
        }
        publisher_->publishFrames(frames);
    }

    if (ingestPipeline_) {
        if (!helloSent_) {
            emit telemetryFrameReady(CarlaTelemetrySerializer::bridgeHello());
            helloSent_ = true;
        }
        for (const QByteArray& frame : frames) {
            emit telemetryFrameReady(frame);
        }
    }

    if (snapshot.hasCamera && !snapshot.camera.isNull()) {
        emit cameraFrameReady(snapshot.camera);
    }

    emit twinSnapshotReady(snapshot);
}

CarlaSimAdapter* CarlaDataBridge::simulatorAdapter() const
{
    if (adapterKind_ != AdapterKind::Simulator) {
        return nullptr;
    }
    return qobject_cast<CarlaSimAdapter*>(adapter_);
}

}  // namespace sdv::dash::carla::integration
