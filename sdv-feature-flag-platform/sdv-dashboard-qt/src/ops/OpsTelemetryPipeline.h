#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace sdv::dash::ops {

class RosBridgeClient;
class SignalManager;
class EgoViewModel;
class TrajectoryViewModel;
class PerceptionViewModel;

/// Wires RosBridgeClient (worker thread) → SignalManager → ViewModels (GUI thread).
class OpsTelemetryPipeline : public QObject {
    Q_OBJECT
public:
    explicit OpsTelemetryPipeline(QObject* parent = nullptr);
    ~OpsTelemetryPipeline() override;

    void start(const QUrl& url = QUrl(QStringLiteral("ws://127.0.0.1:9090")));
    void stop();

    bool isBridgeConnected() const { return bridgeConnected_; }

    SignalManager* signalManager() const { return signalManager_; }
    EgoViewModel* egoViewModel() const { return egoViewModel_; }
    TrajectoryViewModel* trajectoryViewModel() const { return trajectoryViewModel_; }
    PerceptionViewModel* perceptionViewModel() const { return perceptionViewModel_; }

signals:
    void bridgeConnectedChanged(bool connected);
    void statusLineChanged(const QString& line);
    void frontCameraJsonReceived(const QByteArray& utf8Frame);

private:
    void setBridgeConnected(bool connected);
    void scheduleStatusEmit();
    void emitStatusLine();

    QThread workerThread_;
    RosBridgeClient* bridgeClient_{nullptr};
    SignalManager* signalManager_{nullptr};
    EgoViewModel* egoViewModel_{nullptr};
    TrajectoryViewModel* trajectoryViewModel_{nullptr};
    PerceptionViewModel* perceptionViewModel_{nullptr};
    QTimer statusEmitTimer_;
    bool bridgeConnected_{false};
};

}  // namespace sdv::dash::ops
