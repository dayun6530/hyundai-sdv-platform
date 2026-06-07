#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

#ifdef SDV_HAVE_QT_MQTT
class QMqttClient;
#endif

namespace sdv::dash {

class DashboardDataHub : public QObject {
    Q_OBJECT
public:
    explicit DashboardDataHub(QObject* parent = nullptr);
    ~DashboardDataHub() override;

    void setApiBaseUrl(const QString& url);
    void setCarId(const QString& carId);
    void setMqttEndpoint(const QString& host, int port);

    void postCockpitSpeed(double speedKmh);

    QString apiBaseUrl() const { return apiBase_; }
    QString carId() const { return carId_; }

signals:
    void fleetStatesUpdated(const QJsonArray& states);
    void cockpitTelemetryUpdated(const QJsonObject& telemetry);
    void fleetCockpitsUpdated(const QJsonArray& cockpits, int fleetOnline, int fleetTotal);
    void mqttConnectivityChanged(bool connected);
    void mqttFeaturesPayload(const QJsonObject& envelope);
    void timelineRecordsUpdated(const QJsonArray& records);
    void httpLogLine(const QString& line);

private slots:
    void pollFleet();
    void pollCockpit();
    void pollHistory();
    void onFleetReplyFinished();
    void onCockpitReplyFinished();

private:
    QString fleetTopicForCar(const QString& carId) const;
    void setupMqtt();
    void teardownMqtt();

    QNetworkAccessManager* nam_;
    QTimer fleetTimer_;
    QTimer cockpitTimer_;
    QTimer historyTimer_;
    QNetworkReply* fleetReply_{nullptr};
    QNetworkReply* cockpitReply_{nullptr};
    QNetworkReply* speedPostReply_{nullptr};
    qint64 lastSpeedPostMs_{0};
    QString apiBase_{QStringLiteral("http://127.0.0.1:8080")};
    QString carId_{QStringLiteral("car_1")};
    QString mqttHost_{QStringLiteral("127.0.0.1")};
    int mqttPort_{1884};
#ifdef SDV_HAVE_QT_MQTT
    QMqttClient* mqtt_{nullptr};
#endif
};

}  // namespace sdv::dash
