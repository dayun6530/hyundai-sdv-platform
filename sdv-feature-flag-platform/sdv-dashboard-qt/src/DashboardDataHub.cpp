#include "DashboardDataHub.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>

#ifdef SDV_HAVE_QT_MQTT
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttTopicFilter>
#endif

using sdv::dash::DashboardDataHub;

DashboardDataHub::DashboardDataHub(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {
    connect(&fleetTimer_, &QTimer::timeout, this, &DashboardDataHub::pollFleet);
    connect(&historyTimer_, &QTimer::timeout, this, &DashboardDataHub::pollHistory);
    fleetTimer_.start(750);
    historyTimer_.start(2800);
    pollFleet();
    pollHistory();
    setupMqtt();
}

DashboardDataHub::~DashboardDataHub() {
    teardownMqtt();
}

void DashboardDataHub::setApiBaseUrl(const QString& url) {
    apiBase_ = url.trimmed();
    while (apiBase_.endsWith(QLatin1Char('/'))) {
        apiBase_.chop(1);
    }
}

void DashboardDataHub::setCarId(const QString& carId) {
    const QString next = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
    if (carId_ == next) {
        return;
    }
    carId_ = next;
#ifdef SDV_HAVE_QT_MQTT
    if (mqtt_) {
        teardownMqtt();
        setupMqtt();
    }
#endif
}

void DashboardDataHub::setMqttEndpoint(const QString& host, int port) {
    mqttHost_ = host;
    mqttPort_ = port;
#ifdef SDV_HAVE_QT_MQTT
    teardownMqtt();
    setupMqtt();
#endif
}

QString DashboardDataHub::fleetTopicForCar(const QString& carId) const {
    const QString id = carId.startsWith(QLatin1String("car_")) ? carId : (QStringLiteral("car_") + carId);
    return QStringLiteral("/sdv/vehicle/%1/flags/state").arg(id);
}

void DashboardDataHub::pollFleet() {
    if (fleetReply_) {
        return;
    }
    QUrl url(apiBase_ + QStringLiteral("/api/fleet/state"));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    fleetReply_ = nam_->get(req);
    connect(fleetReply_, &QNetworkReply::finished, this, &DashboardDataHub::onFleetReplyFinished);
}

void DashboardDataHub::onFleetReplyFinished() {
    auto* reply = fleetReply_;
    fleetReply_ = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit httpLogLine(QStringLiteral("[fleet/state] HTTP error: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray body = reply->readAll();
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        emit httpLogLine(QStringLiteral("[fleet/state] Non-JSON response"));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray states = root.value(QStringLiteral("states")).toArray();
    emit fleetStatesUpdated(states);
}

void DashboardDataHub::pollHistory() {
    QUrl url(apiBase_ + QStringLiteral("/api/fleet/fleet-history"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("120"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit httpLogLine(QStringLiteral("[fleet-history] HTTP error: %1").arg(reply->errorString()));
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            return;
        }
        const QJsonArray rec = doc.object().value(QStringLiteral("records")).toArray();
        emit timelineRecordsUpdated(rec);
    });
}

void DashboardDataHub::setupMqtt() {
#ifdef SDV_HAVE_QT_MQTT
    mqtt_ = new QMqttClient(this);
    mqtt_->setHostname(mqttHost_);
    mqtt_->setPort(static_cast<quint16>(mqttPort_));
    mqtt_->setClientId(QStringLiteral("sdv_qt_dashboard_%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')));

    QObject::connect(mqtt_, &QMqttClient::connected, this, [this]() {
        emit mqttConnectivityChanged(true);
        emit httpLogLine(QStringLiteral("[MQTT] Connected %1:%2").arg(mqttHost_).arg(mqttPort_));
        const QString topic = fleetTopicForCar(carId_);
        auto* sub = mqtt_->subscribe(QMqttTopicFilter(topic), 1);
        if (!sub) {
            emit httpLogLine(QStringLiteral("[MQTT] subscribe failed for ") + topic);
        } else {
            emit httpLogLine(QStringLiteral("[MQTT] Subscribed ") + topic);
        }
    });
    QObject::connect(mqtt_, &QMqttClient::disconnected, this, [this]() {
        emit mqttConnectivityChanged(false);
        emit httpLogLine(QStringLiteral("[MQTT] Disconnected"));
    });
    QObject::connect(mqtt_, &QMqttClient::messageReceived, this,
                     [this](const QByteArray& message, const QMqttTopicName& topic) {
                         Q_UNUSED(topic);
                         const auto doc = QJsonDocument::fromJson(message);
                         if (!doc.isObject()) {
                             return;
                         }
                         emit mqttFeaturesPayload(doc.object());
                     });
    mqtt_->connectToHost();
#else
    emit mqttConnectivityChanged(false);
#endif
}

void DashboardDataHub::teardownMqtt() {
#ifdef SDV_HAVE_QT_MQTT
    if (mqtt_) {
        mqtt_->disconnectFromHost();
        mqtt_->deleteLater();
        mqtt_ = nullptr;
    }
#endif
}
