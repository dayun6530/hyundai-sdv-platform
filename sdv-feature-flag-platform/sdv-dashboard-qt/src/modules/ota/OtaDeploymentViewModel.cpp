#include "modules/ota/OtaDeploymentViewModel.h"

#include "modules/ota/OtaDeploymentService.h"

namespace sdv::dash::ota {

OtaDeploymentViewModel::OtaDeploymentViewModel(OtaDeploymentService* service, QObject* parent)
    : QObject(parent), service_(service)
{
    connect(service_, &OtaDeploymentService::phaseChanged, this, [this](OtaPhase phase, int progress) {
        setCurrentState(otaPhaseToString(phase));
        setProgress(progress);
        busy_ = phase != OtaPhase::Idle && phase != OtaPhase::Completed && phase != OtaPhase::Failed;
        emit changed();
    });
    connect(service_, &OtaDeploymentService::activeVersionChanged, this, [this](const QString& v) {
        activeVersion_ = v;
        emit changed();
    });
    connect(service_, &OtaDeploymentService::targetVersionChanged, this, [this](const QString& v) {
        targetVersion_ = v;
        emit changed();
    });
    connect(service_, &OtaDeploymentService::logAppended, this, &OtaDeploymentViewModel::onLog);

    availableVersions_ = service_->catalogVersions();
    refreshFromService();
}

void OtaDeploymentViewModel::refreshFromService()
{
    activeVersion_ = service_->activeVersion();
    targetVersion_ = service_->targetVersion();
    setCurrentState(otaPhaseToString(service_->phase()));
    setProgress(service_->progress());
    busy_ = service_->phase() != OtaPhase::Idle && service_->phase() != OtaPhase::Completed &&
            service_->phase() != OtaPhase::Failed;
    emit changed();
}

void OtaDeploymentViewModel::setCurrentState(const QString& state)
{
    if (currentState_ == state) {
        return;
    }
    currentState_ = state;
    emit changed();
}

void OtaDeploymentViewModel::setProgress(const int value)
{
    const int clamped = qBound(0, value, 100);
    if (progress_ == clamped) {
        return;
    }
    progress_ = clamped;
    emit changed();
}

void OtaDeploymentViewModel::deploySelectedVersion(const QString& version)
{
    service_->deployVersion(version);
    refreshFromService();
}

void OtaDeploymentViewModel::requestRollback()
{
    service_->requestRollback();
    refreshFromService();
}

void OtaDeploymentViewModel::resetDeployment()
{
    service_->resetDeployment();
    refreshFromService();
}

void OtaDeploymentViewModel::ingestCarState(const QJsonObject& carState)
{
    service_->ingestCarState(carState);
    refreshFromService();
}

void OtaDeploymentViewModel::onLog(const QString& line)
{
    emit logLine(line);
}

}  // namespace sdv::dash::ota
