#pragma once

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QString>
#include <QTimer>

class QWebEngineView;

namespace sdv::dash {

class DashboardDataHub;
class FeatureStatusWidget;
class OTADeploymentWidget;
class ABTestingWidget;
class ShadowCompareWidget;
class HealthMonitorWidget;
class EventTimelineWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void applyLaunchOptions(const QString& apiBase,
                            const QString& carId,
                            const QString& mqttHost,
                            int mqttPort);

private slots:
    void onFleetStates(const QJsonArray& states);
    void onMqttFeatures(const QJsonObject& env);
    void onTimeline(const QJsonArray& records);
    void onHttpLine(const QString& line);
    void onCarComboChanged(int idx);
    void onCockpitCarSelected(const QString& carId);
    void onTrafficAnimTick();

private:
    QJsonObject pickCar(const QJsonArray& states, const QString& carId) const;
    void syncCockpitState(const QJsonObject& carState);

    DashboardDataHub* hub_{nullptr};
    QWebEngineView* cockpit_{nullptr};
    FeatureStatusWidget* features_{nullptr};
    OTADeploymentWidget* ota_{nullptr};
    ABTestingWidget* ab_{nullptr};
    ShadowCompareWidget* shadow_{nullptr};
    HealthMonitorWidget* health_{nullptr};
    EventTimelineWidget* timeline_{nullptr};

    QComboBox* carCombo_{nullptr};
    QLabel* mqttLed_{nullptr};
    QJsonArray lastFleet_;
    QJsonObject lastMqtt_;
    QString carId_;
    QTimer trafficAnim_;
};

}  // namespace sdv::dash
