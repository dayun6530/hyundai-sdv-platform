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

    QString apiBaseUrl() const { return apiBase_; }
    QString carId() const { return carId_; }

signals:
    void fleetStatesUpdated(const QJsonArray& states);
    void mqttConnectivityChanged(bool connected);
    void mqttFeaturesPayload(const QJsonObject& envelope);
    void timelineRecordsUpdated(const QJsonArray& records);
    void httpLogLine(const QString& line);

private slots:
    void pollFleet();
    void pollHistory();
    void onFleetReplyFinished();

private:
    QString fleetTopicForCar(const QString& carId) const;
    void setupMqtt();
    void teardownMqtt();

    QNetworkAccessManager* nam_;
    QTimer fleetTimer_;
    QTimer historyTimer_;
    QNetworkReply* fleetReply_{nullptr};
    QString apiBase_{QStringLiteral("http://127.0.0.1:8080")};
    QString carId_{QStringLiteral("car_1")};
    QString mqttHost_{QStringLiteral("127.0.0.1")};
    int mqttPort_{1884};
#ifdef SDV_HAVE_QT_MQTT
    QMqttClient* mqtt_{nullptr};
#endif
};

}  // namespace sdv::dash
