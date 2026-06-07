#include "modules/ota/RollbackController.h"

#include "modules/ota/OtaStateMachine.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace sdv::dash::ota {

RollbackController::RollbackController(OtaStateMachine* stateMachine, QObject* parent)
    : QObject(parent), stateMachine_(stateMachine), nam_(new QNetworkAccessManager(this))
{
    connect(nam_, &QNetworkAccessManager::finished, this, &RollbackController::onHttpFinished);
}

void RollbackController::setApiBase(const QString& url)
{
    apiBase_ = url.trimmed();
    while (apiBase_.endsWith(QLatin1Char('/'))) {
        apiBase_.chop(1);
    }
    useSimulation_ = apiBase_.isEmpty();
}

void RollbackController::setCarId(const QString& carId)
{
    carId_ = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
}

void RollbackController::requestRollback(const QString& activeVersion)
{
    if (active_ || !stateMachine_) {
        return;
    }

    active_ = true;
    restoreVersion_ = activeVersion;
    emit rollbackStarted();
    emit logLine(QStringLiteral("[OTA] Rollback requested → %1").arg(activeVersion.isEmpty() ? QStringLiteral("previous") : activeVersion));

    stateMachine_->startRollback();

    if (useSimulation_ || carId_.isEmpty()) {
        startSimulation();
        return;
    }

    QUrl url(apiBase_ + QStringLiteral("/api/fleet/action"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body.insert(QStringLiteral("action"), QStringLiteral("rollback"));
    body.insert(QStringLiteral("car_id"), carId_);
    body.insert(QStringLiteral("feature"), featureName_);

    nam_->post(req, QJsonDocument(body).toJson());
}

void RollbackController::startSimulation()
{
    simTicks_ = 0;
    QTimer::singleShot(900, this, &RollbackController::onSimStep);
}

void RollbackController::onSimStep()
{
    if (!active_) {
        return;
    }
    ++simTicks_;
    if (simTicks_ < 2) {
        QTimer::singleShot(700, this, &RollbackController::onSimStep);
        return;
    }
    finishLocal(true, QStringLiteral("Rollback complete (simulated)"));
}

void RollbackController::onHttpFinished(QNetworkReply* reply)
{
    if (!active_) {
        reply->deleteLater();
        return;
    }

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    const bool ok = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300;
    if (ok) {
        finishLocal(true, QString::fromUtf8(raw));
    } else {
        emit logLine(QStringLiteral("[OTA] Rollback HTTP %1 — falling back to local simulation").arg(code));
        startSimulation();
    }
}

void RollbackController::finishLocal(const bool success, const QString& detail)
{
    active_ = false;
    stateMachine_->resetToIdle();
    if (success) {
        emit logLine(QStringLiteral("[OTA] Rollback finished — active %1").arg(restoreVersion_));
    } else {
        emit logLine(QStringLiteral("[OTA] Rollback failed: %1").arg(detail));
    }
    emit rollbackFinished(success, detail);
}

}  // namespace sdv::dash::ota
