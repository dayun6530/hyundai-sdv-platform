#include "modules/ota/OtaStateMachine.h"

namespace sdv::dash::ota {

namespace {

void addEdge(QHash<OtaPhase, QSet<OtaPhase>>* edges, const OtaPhase from, const OtaPhase to)
{
    (*edges)[from].insert(to);
}

}  // namespace

OtaStateMachine::OtaStateMachine(QObject* parent) : QObject(parent)
{
    addEdge(&edges_, OtaPhase::Idle, OtaPhase::Downloading);
    addEdge(&edges_, OtaPhase::Downloading, OtaPhase::Verifying);
    addEdge(&edges_, OtaPhase::Verifying, OtaPhase::Installing);
    addEdge(&edges_, OtaPhase::Installing, OtaPhase::Switching);
    addEdge(&edges_, OtaPhase::Switching, OtaPhase::Validating);
    addEdge(&edges_, OtaPhase::Validating, OtaPhase::Completed);

    addEdge(&edges_, OtaPhase::Idle, OtaPhase::Rollback);
    addEdge(&edges_, OtaPhase::Completed, OtaPhase::Rollback);
    addEdge(&edges_, OtaPhase::Failed, OtaPhase::Rollback);

    addEdge(&edges_, OtaPhase::Downloading, OtaPhase::Failed);
    addEdge(&edges_, OtaPhase::Verifying, OtaPhase::Failed);
    addEdge(&edges_, OtaPhase::Installing, OtaPhase::Failed);
    addEdge(&edges_, OtaPhase::Switching, OtaPhase::Failed);
    addEdge(&edges_, OtaPhase::Validating, OtaPhase::Failed);

    addEdge(&edges_, OtaPhase::Completed, OtaPhase::Idle);
    addEdge(&edges_, OtaPhase::Failed, OtaPhase::Idle);
    addEdge(&edges_, OtaPhase::Rollback, OtaPhase::Idle);

    const OtaPhase all[] = {
        OtaPhase::Idle,       OtaPhase::Downloading, OtaPhase::Verifying, OtaPhase::Installing,
        OtaPhase::Switching,  OtaPhase::Validating,  OtaPhase::Completed, OtaPhase::Failed,
        OtaPhase::Rollback,
    };
    for (const OtaPhase p : all) {
        addEdge(&edges_, p, p);
    }
}

bool OtaStateMachine::canTransition(const OtaPhase from, const OtaPhase to) const
{
    return edges_.value(from).contains(to);
}

bool OtaStateMachine::isBusy() const
{
    return phase_ != OtaPhase::Idle && phase_ != OtaPhase::Completed && phase_ != OtaPhase::Failed;
}

void OtaStateMachine::applyPhase(const OtaPhase phase, const bool force)
{
    if (phase_ == phase) {
        return;
    }
    if (!force && !canTransition(phase_, phase)) {
        return;
    }
    phase_ = phase;
    emit phaseChanged(phase_, otaPhaseProgress(phase_));

    switch (phase) {
    case OtaPhase::Downloading:
        emitPhaseLog(QStringLiteral("Downloading"), targetVersion_);
        break;
    case OtaPhase::Verifying:
        emitPhaseLog(QStringLiteral("Verifying package"));
        break;
    case OtaPhase::Installing:
        emitPhaseLog(QStringLiteral("Installing"));
        break;
    case OtaPhase::Switching:
        emitPhaseLog(QStringLiteral("Switching slot B"));
        break;
    case OtaPhase::Validating:
        emitPhaseLog(QStringLiteral("Validating"));
        break;
    case OtaPhase::Completed:
        emitPhaseLog(QStringLiteral("Validation passed"));
        break;
    case OtaPhase::Failed:
        emit deploymentLog(QStringLiteral("[OTA] Deployment failed%1")
                               .arg(lastFailReason_.isEmpty() ? QString() : QStringLiteral(": ") + lastFailReason_));
        emit failed(lastFailReason_);
        break;
    case OtaPhase::Rollback:
        emitPhaseLog(QStringLiteral("Rollback in progress"));
        break;
    case OtaPhase::Idle:
    default:
        break;
    }
}

void OtaStateMachine::transitionTo(const OtaPhase phase)
{
    applyPhase(phase, false);
}

void OtaStateMachine::startDeploy()
{
    if (!canTransition(phase_, OtaPhase::Downloading)) {
        return;
    }
    transitionTo(OtaPhase::Downloading);
}

void OtaStateMachine::advanceStep()
{
    OtaPhase next = phase_;
    switch (phase_) {
    case OtaPhase::Downloading:
        next = OtaPhase::Verifying;
        break;
    case OtaPhase::Verifying:
        next = OtaPhase::Installing;
        break;
    case OtaPhase::Installing:
        next = OtaPhase::Switching;
        break;
    case OtaPhase::Switching:
        next = OtaPhase::Validating;
        break;
    case OtaPhase::Validating:
        next = OtaPhase::Completed;
        break;
    default:
        return;
    }
    transitionTo(next);
}

void OtaStateMachine::markFailed(const QString& reason)
{
    lastFailReason_ = reason;
    if (canTransition(phase_, OtaPhase::Failed)) {
        transitionTo(OtaPhase::Failed);
    }
}

void OtaStateMachine::startRollback()
{
    if (canTransition(phase_, OtaPhase::Rollback)) {
        transitionTo(OtaPhase::Rollback);
    }
}

void OtaStateMachine::resetToIdle()
{
    if (canTransition(phase_, OtaPhase::Idle)) {
        transitionTo(OtaPhase::Idle);
    }
}

void OtaStateMachine::syncPhase(const OtaPhase phase)
{
    applyPhase(phase, true);
}

void OtaStateMachine::emitPhaseLog(const QString& action, const QString& detail)
{
    QString line = QStringLiteral("[OTA] %1").arg(action);
    if (!detail.isEmpty()) {
        line.append(QLatin1Char(' ')).append(detail);
    }
    emit deploymentLog(line);
}

}  // namespace sdv::dash::ota
