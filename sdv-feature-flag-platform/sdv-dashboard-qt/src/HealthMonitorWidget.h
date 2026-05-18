#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;

namespace sdv::dash {

class HealthMonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit HealthMonitorWidget(QWidget* parent = nullptr);
    void updateFromCarState(const QJsonObject& carState);

private:
    QLabel* cpu_{nullptr};
    QLabel* mem_{nullptr};
    QLabel* events_{nullptr};
    QLabel* heartbeat_{nullptr};
    QLabel* queue_{nullptr};
    QLabel* latency_{nullptr};
    qint64 lastWallMs_{0};
};

}  // namespace sdv::dash
