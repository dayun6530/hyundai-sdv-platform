#pragma once

#include <QJsonArray>
#include <QWidget>

class QLabel;
class QProgressBar;

namespace sdv::dash {

class TrafficSplitBar;

class ABTestingWidget : public QWidget {
    Q_OBJECT
public:
    explicit ABTestingWidget(QWidget* parent = nullptr);
    void updateRollup(const QJsonArray& fleetStates);

public slots:
    void tickTrafficShift();

private:
    QLabel* strategy_{nullptr};
    QLabel* canary_{nullptr};
    QLabel* versionA_{nullptr};
    QLabel* versionB_{nullptr};
    QLabel* pctA_{nullptr};
    QLabel* pctB_{nullptr};
    TrafficSplitBar* splitBar_{nullptr};
    QProgressBar* barA_{nullptr};
    QProgressBar* barB_{nullptr};
    float anim_{0.f};
    int pctATotal_{0};
};

}  // namespace sdv::dash
