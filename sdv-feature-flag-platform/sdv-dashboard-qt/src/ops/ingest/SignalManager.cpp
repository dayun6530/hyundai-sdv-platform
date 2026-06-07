#include "ops/ingest/SignalManager.h"

#include "ops/domain/TelemetryParser.h"

namespace sdv::dash::ops {

SignalManager::SignalManager(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<sdv::dash::ops::VehicleState>("sdv::dash::ops::VehicleState");
    qRegisterMetaType<sdv::dash::ops::EgoPose>("sdv::dash::ops::EgoPose");
    qRegisterMetaType<sdv::dash::ops::Trajectory>("sdv::dash::ops::Trajectory");
    qRegisterMetaType<sdv::dash::ops::PerceptionObjectList>("sdv::dash::ops::PerceptionObjectList");
}

void SignalManager::ingestJsonFrame(const QByteArray& utf8Frame)
{
    ++framesReceived_;

    const auto envelopeOpt = TelemetryParser::parseFrame(utf8Frame);
    if (!envelopeOpt.has_value()) {
        ++framesDropped_;
        emit parseError(QStringLiteral("Invalid telemetry JSON frame"));
        return;
    }

    const ParsedTelemetryEnvelope& envelope = envelopeOpt.value();
    if (envelope.type == QLatin1String("bridge_hello")) {
        emit bridgeHelloReceived();
        return;
    }

    const qint64 stampNs = envelope.stampNs > 0
        ? envelope.stampNs
        : envelope.data.value(QStringLiteral("stamp_ns")).toVariant().toLongLong();

    if (envelope.type == QLatin1String("vehicle_state")) {
        const VehicleState state = VehicleState::fromJson(envelope.data, stampNs, envelope.frameId);
        emit vehicleStateUpdated(state);
        return;
    }

    if (envelope.type == QLatin1String("ego_pose")) {
        const EgoPose pose = EgoPose::fromJson(envelope.data, stampNs, envelope.frameId);
        emit egoPoseUpdated(pose);
        return;
    }

    if (envelope.type == QLatin1String("trajectory")) {
        const Trajectory trajectory = Trajectory::fromJson(envelope.data, stampNs, envelope.frameId);
        emit trajectoryUpdated(trajectory);
        return;
    }

    if (envelope.type == QLatin1String("perception_objects")) {
        const PerceptionObjectList objects =
            PerceptionObjectList::fromJson(envelope.data, stampNs, envelope.frameId);
        emit perceptionUpdated(objects);
        return;
    }

    ++framesDropped_;
}

}  // namespace sdv::dash::ops
