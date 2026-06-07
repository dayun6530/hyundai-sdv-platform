#include "modules/ota/OtaDeploymentService.h"

#include "modules/ota/OtaStateMachine.h"
#include "modules/ota/RollbackController.h"

#include <QTimer>

namespace sdv::dash::ota {

OtaDeploymentService::OtaDeploymentService(QObject* parent) : QObject(parent)
{
    catalog_ = {
        QStringLiteral("v2.0.0"),
        QStringLiteral("v2.1.0"),
        QStringLiteral("v3.0.0"),
        QStringLiteral("v4.0.0"),
        QStringLiteral("v5.0.0"),
        QStringLiteral("v6.0.0"),
        QStringLiteral("v7.0.0"),
    };

    stateMachine_ = new OtaStateMachine(this);
    rollback_ = new RollbackController(stateMachine_, this);
    bindRollbackLogs();

    connect(stateMachine_, &OtaStateMachine::phaseChanged, this, &OtaDeploymentService::onPhaseChanged);
    connect(stateMachine_, &OtaStateMachine::deploymentLog, this, &OtaDeploymentService::onMachineLog);
    connect(stateMachine_, &OtaStateMachine::failed, this, [this](const QString& reason) {
        stopSimulation();
        userDeployActive_ = false;
        emit deploymentFinished(false);
        emit logAppended(QStringLiteral("[OTA] %1").arg(reason));
    });
    connect(rollback_, &RollbackController::rollbackFinished, this, &OtaDeploymentService::onRollbackFinished);
}

void OtaDeploymentService::bindRollbackLogs()
{
    connect(rollback_, &RollbackController::logLine, this, &OtaDeploymentService::logAppended);
}

void OtaDeploymentService::setApiBase(const QString& url)
{
    rollback_->setApiBase(url);
}

void OtaDeploymentService::setCarId(const QString& carId)
{
    carId_ = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
    rollback_->setCarId(carId_);
}

OtaPhase OtaDeploymentService::phase() const
{
    return stateMachine_->phase();
}

int OtaDeploymentService::progress() const
{
    return otaPhaseProgress(stateMachine_->phase());
}

void OtaDeploymentService::ingestCarState(const QJsonObject& carState)
{
    applyVersionsFromCar(carState);

    if (userDeployActive_ || simulating_ || rollback_->isActive()) {
        return;
    }

    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    const OtaPhase remote = phaseFromCarOta(ota);
    if (remote != stateMachine_->phase()) {
        stateMachine_->syncPhase(remote);
    }
}

void OtaDeploymentService::applyVersionsFromCar(const QJsonObject& carState)
{
    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
    const QString active = adas.value(QStringLiteral("active_version")).toString();
    const QString pending = adas.value(QStringLiteral("pending_active_version")).toString();

    if (!active.isEmpty() && active != activeVersion_) {
        activeVersion_ = active;
        emit activeVersionChanged(activeVersion_);
    }

    const QString nextTarget = pending.isEmpty() ? targetVersion_ : pending;
    if (!nextTarget.isEmpty() && nextTarget != targetVersion_) {
        targetVersion_ = nextTarget;
        emit targetVersionChanged(targetVersion_);
    }
}

OtaPhase OtaDeploymentService::phaseFromCarOta(const QJsonObject& ota) const
{
    const QString mode = ota.value(QStringLiteral("update_mode")).toString();
    if (mode.compare(QStringLiteral("FAILED"), Qt::CaseInsensitive) == 0) {
        return OtaPhase::Failed;
    }
    if (mode.compare(QStringLiteral("READY"), Qt::CaseInsensitive) == 0) {
        return OtaPhase::Completed;
    }

    const QString phase = ota.value(QStringLiteral("ota_phase")).toString();
    if (phase.isEmpty()) {
        return OtaPhase::Idle;
    }
    return otaPhaseFromString(phase);
}

void OtaDeploymentService::deployVersion(const QString& version)
{
    const QString target = version.trimmed();
    if (target.isEmpty() || target == activeVersion_) {
        return;
    }

    targetVersion_ = target;
    emit targetVersionChanged(targetVersion_);

    userDeployActive_ = true;
    emit logAppended(QStringLiteral("[OTA] Deploy requested → %1").arg(targetVersion_));
    startSimulation(target);
}

void OtaDeploymentService::startSimulation(const QString& target)
{
    stopSimulation();
    targetVersion_ = target;
    emit targetVersionChanged(targetVersion_);

    simulating_ = true;
    simStep_ = 0;
    stateMachine_->setTargetVersion(target);
    stateMachine_->syncPhase(OtaPhase::Idle);

    QTimer::singleShot(250, this, [this]() { stateMachine_->startDeploy(); });
    QTimer::singleShot(1200, this, &OtaDeploymentService::onSimTick);
}

void OtaDeploymentService::stopSimulation()
{
    simulating_ = false;
    simStep_ = 0;
}

void OtaDeploymentService::onSimTick()
{
    if (!simulating_) {
        return;
    }

    ++simStep_;
    if (stateMachine_->phase() != OtaPhase::Completed) {
        stateMachine_->advanceStep();
    }

    const OtaPhase p = stateMachine_->phase();
    if (p == OtaPhase::Completed) {
        activeVersion_ = targetVersion_;
        emit activeVersionChanged(activeVersion_);
        stopSimulation();
        userDeployActive_ = false;
        emit deploymentFinished(true);
        emit logAppended(QStringLiteral("[OTA] Deployment completed — active %1").arg(activeVersion_));
        return;
    }

    if (p == OtaPhase::Failed) {
        stopSimulation();
        userDeployActive_ = false;
        emit deploymentFinished(false);
        return;
    }

    const int delayMs = (p == OtaPhase::Downloading) ? 1100 : 850;
    QTimer::singleShot(delayMs, this, &OtaDeploymentService::onSimTick);
}

void OtaDeploymentService::requestRollback()
{
    if (rollback_->isActive()) {
        return;
    }
    rollback_->requestRollback(activeVersion_);
}

void OtaDeploymentService::resetDeployment()
{
    stopSimulation();
    userDeployActive_ = false;
    targetVersion_.clear();
    emit targetVersionChanged(targetVersion_);
    stateMachine_->resetToIdle();
    stateMachine_->syncPhase(OtaPhase::Idle);
    emit logAppended(QStringLiteral("[OTA] Deployment reset"));
}

void OtaDeploymentService::onPhaseChanged(const OtaPhase phase, const int progress)
{
    emit phaseChanged(phase, progress);
}

void OtaDeploymentService::onMachineLog(const QString& line)
{
    emit logAppended(line);
}

void OtaDeploymentService::onRollbackFinished(const bool success, const QString& detail)
{
    Q_UNUSED(detail);
    userDeployActive_ = false;
    stopSimulation();
    if (success && !rollback_->isActive()) {
        const int idx = catalog_.indexOf(activeVersion_);
        if (idx > 0) {
            activeVersion_ = catalog_.at(idx - 1);
            emit activeVersionChanged(activeVersion_);
        }
        targetVersion_.clear();
        emit targetVersionChanged(targetVersion_);
    }
    emit deploymentFinished(success);
}

}  // namespace sdv::dash::ota
