#include "DashboardDataHub.h"

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>
#include <QtMath>
#include <QRegularExpression>

#ifdef SDV_HAVE_QT_MQTT
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttTopicFilter>
#endif

using sdv::dash::DashboardDataHub;

namespace {

bool shouldEmitHttpError(const QString& key)
{
    static QHash<QString, qint64> lastMs;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 prev = lastMs.value(key, 0);
    if (now - prev < 15000) {
        return false;
    }
    lastMs[key] = now;
    return true;
}

QString formatHttpError(const QString& endpoint, const QString& errorString, const QString& apiBase)
{
    QString line = QStringLiteral("[%1] HTTP error: %2").arg(endpoint, errorString);
    if (errorString.contains(QStringLiteral("refused"), Qt::CaseInsensitive)) {
        line += QStringLiteral(" — status-server not running at %1 (see below)").arg(apiBase);
    }
    return line;
}

}  // namespace

DashboardDataHub::DashboardDataHub(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {
    connect(&fleetTimer_, &QTimer::timeout, this, &DashboardDataHub::pollFleet);
    connect(&cockpitTimer_, &QTimer::timeout, this, &DashboardDataHub::pollCockpit);
    connect(&historyTimer_, &QTimer::timeout, this, &DashboardDataHub::pollHistory);
    fleetTimer_.start(400);
    cockpitTimer_.start(500);
    historyTimer_.start(2800);
    pollFleet();
    pollCockpit();
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
    pollCockpit();
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

void DashboardDataHub::postCockpitSpeed(const double speedKmh)
{
    if (apiBase_.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastSpeedPostMs_ < 350) {
        return;
    }
    lastSpeedPostMs_ = now;
    if (speedPostReply_) {
        return;
    }

    QUrl url(apiBase_ + QStringLiteral("/api/fleet/cockpit/speed"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");

    QJsonObject body;
    body.insert(QStringLiteral("car_id"), carId_);
    body.insert(QStringLiteral("speed_kmh"), qRound(qMax(0.0, speedKmh)));
    body.insert(QStringLiteral("source"), QStringLiteral("SDV_Dashboard_pygame"));

    speedPostReply_ = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(speedPostReply_, &QNetworkReply::finished, this, [this]() {
        if (speedPostReply_) {
            speedPostReply_->deleteLater();
            speedPostReply_ = nullptr;
        }
    });
}

void DashboardDataHub::pollCockpit() {
    if (cockpitReply_ || apiBase_.isEmpty()) {
        return;
    }
    QUrl url(apiBase_ + QStringLiteral("/api/fleet/cockpit/all"));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    cockpitReply_ = nam_->get(req);
    connect(cockpitReply_, &QNetworkReply::finished, this, &DashboardDataHub::onCockpitReplyFinished);
}

void DashboardDataHub::onCockpitReplyFinished() {
    auto* reply = cockpitReply_;
    cockpitReply_ = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (shouldEmitHttpError(QStringLiteral("fleet/cockpit/all"))) {
            emit httpLogLine(formatHttpError(QStringLiteral("fleet/cockpit/all"), reply->errorString(), apiBase_));
        }
        return;
    }
    const auto doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray cockpits = root.value(QStringLiteral("cockpits")).toArray();
    const int fleetOnline = root.value(QStringLiteral("fleet_online")).toInt(0);
    const int fleetTotal = root.value(QStringLiteral("fleet_total")).toInt(10);
    emit fleetCockpitsUpdated(cockpits, fleetOnline, fleetTotal);
    for (const auto& item : cockpits) {
        const QJsonObject o = item.toObject();
        if (o.value(QStringLiteral("car_id")).toString() == carId_) {
            emit cockpitTelemetryUpdated(o);
            break;
        }
    }
}

void DashboardDataHub::onFleetReplyFinished() {
    auto* reply = fleetReply_;
    fleetReply_ = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (shouldEmitHttpError(QStringLiteral("fleet/state"))) {
            emit httpLogLine(formatHttpError(QStringLiteral("fleet/state"), reply->errorString(), apiBase_));
        }
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
            if (shouldEmitHttpError(QStringLiteral("fleet-history"))) {
                emit httpLogLine(formatHttpError(QStringLiteral("fleet-history"), reply->errorString(), apiBase_));
            }
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
        auto* sub = mqtt_->subscribe(QMqttTopicFilter(QStringLiteral("/sdv/vehicle/+/flags/state")), 1);
        if (!sub) {
            emit httpLogLine(QStringLiteral("[MQTT] subscribe failed for fleet wildcard"));
        } else {
            emit httpLogLine(QStringLiteral("[MQTT] Subscribed /sdv/vehicle/+/flags/state"));
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
                         QJsonObject env = doc.object();
                         const QRegularExpression re(QStringLiteral(R"(/sdv/vehicle/(car_\d+)/flags/state)"));
                         const auto match = re.match(topic.name());
                         if (match.hasMatch()) {
                             env.insert(QStringLiteral("car_id"), match.captured(1));
                         }
                         emit mqttFeaturesPayload(env);
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
