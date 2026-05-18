#pragma once

#include <QJsonArray>
#include <QWidget>

class QLabel;
class QProgressBar;
class QSlider;

namespace sdv::dash {

class ABTestingWidget : public QWidget {
    Q_OBJECT
public:
    explicit ABTestingWidget(QWidget* parent = nullptr);
    void updateRollup(const QJsonArray& fleetStates);

private:
    QLabel* strategy_{nullptr};
    QLabel* canary_{nullptr};
    QProgressBar* barA_{nullptr};
    QProgressBar* barB_{nullptr};
    QLabel* pctA_{nullptr};
    QLabel* pctB_{nullptr};
    QSlider* sliderDemo_{nullptr};
    /** Visual-only animation phase driven by timer in MainWindow */
public slots:
    void tickTrafficShift();

private:
    float anim_{0.f};
};

}  // namespace sdv::dash
