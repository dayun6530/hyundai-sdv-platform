#pragma once

#include "modules/ota/OtaTypes.h"

#include <QHash>
#include <QObject>
#include <QSet>

namespace sdv::dash::ota {

/**
 * Signal-driven OTA state machine with the same transition graph as a QStateMachine deployment flow.
 * (Requires Qt6::StateMachine when available; uses an equivalent transition table otherwise.)
 */
class OtaStateMachine : public QObject {
    Q_OBJECT
public:
    explicit OtaStateMachine(QObject* parent = nullptr);

    OtaPhase phase() const { return phase_; }
    bool isBusy() const;
    QString targetVersion() const { return targetVersion_; }

    void setTargetVersion(const QString& version) { targetVersion_ = version; }

public slots:
    void startDeploy();
    void advanceStep();
    void markFailed(const QString& reason = QString());
    void startRollback();
    void resetToIdle();
    void syncPhase(OtaPhase phase);

signals:
    void phaseChanged(OtaPhase phase, int progress);
    void deploymentLog(const QString& line);
    void failed(const QString& reason);

private:
    bool canTransition(OtaPhase from, OtaPhase to) const;
    void applyPhase(OtaPhase phase, bool force);
    void transitionTo(OtaPhase phase);
    void emitPhaseLog(const QString& action, const QString& detail = QString());

    QHash<OtaPhase, QSet<OtaPhase>> edges_;
    OtaPhase phase_{OtaPhase::Idle};
    QString targetVersion_;
    QString lastFailReason_;
};

}  // namespace sdv::dash::ota
