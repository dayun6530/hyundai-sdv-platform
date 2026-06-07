#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QProgressBar;

namespace sdv::dash {

class HealthMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit HealthMonitorWidget(QWidget* parent = nullptr);
    void updateFromCarState(const QJsonObject& carState);
    void setRosTelemetryLine(const QString& line);

public slots:
    void noteCameraFrame();
    void noteFleetTick();

private:
    void refreshRates();

    QProgressBar* cpuBar_{nullptr};
    QProgressBar* memBar_{nullptr};
    QProgressBar* fpsBar_{nullptr};
    QProgressBar* latencyBar_{nullptr};
    QProgressBar* topicBar_{nullptr};
    QProgressBar* processBar_{nullptr};
    QLabel* cpu_{nullptr};
    QLabel* mem_{nullptr};
    QLabel* fps_{nullptr};
    QLabel* latency_{nullptr};
    QLabel* topicRate_{nullptr};
    QLabel* processCount_{nullptr};
    QLabel* events_{nullptr};
    QLabel* heartbeat_{nullptr};
    QLabel* rosTelemetry_{nullptr};

    qint64 lastWallMs_{0};
    qint64 lastFleetMs_{0};
    int frameCount_{0};
    int tickCount_{0};
    qint64 fpsWindowStart_{0};
    qint64 tickWindowStart_{0};
    int displayFps_{0};
    int displayTopicHz_{0};
    int displayLatencyMs_{0};
};

}  // namespace sdv::dash
