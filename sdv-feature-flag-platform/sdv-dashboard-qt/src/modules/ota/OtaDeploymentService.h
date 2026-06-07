#pragma once

#include "modules/ota/OtaTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace sdv::dash::ota {

class OtaStateMachine;
class RollbackController;

class OtaDeploymentService : public QObject {
    Q_OBJECT
public:
    explicit OtaDeploymentService(QObject* parent = nullptr);

    OtaStateMachine* stateMachine() const { return stateMachine_; }
    RollbackController* rollbackController() const { return rollback_; }

    QString activeVersion() const { return activeVersion_; }
    QString targetVersion() const { return targetVersion_; }
    OtaPhase phase() const;
    int progress() const;
    bool isSimulating() const { return simulating_; }
    QStringList catalogVersions() const { return catalog_; }

    void setApiBase(const QString& url);
    void setCarId(const QString& carId);

public slots:
    void ingestCarState(const QJsonObject& carState);
    void deployVersion(const QString& version);
    void requestRollback();
    void resetDeployment();

signals:
    void activeVersionChanged(const QString& version);
    void targetVersionChanged(const QString& version);
    void phaseChanged(OtaPhase phase, int progress);
    void logAppended(const QString& line);
    void deploymentFinished(bool success);

private slots:
    void onPhaseChanged(OtaPhase phase, int progress);
    void onMachineLog(const QString& line);
    void onSimTick();
    void onRollbackFinished(bool success, const QString& detail);

private:
    void applyVersionsFromCar(const QJsonObject& carState);
    OtaPhase phaseFromCarOta(const QJsonObject& ota) const;
    void startSimulation(const QString& target);
    void stopSimulation();
    void bindRollbackLogs();

    OtaStateMachine* stateMachine_{nullptr};
    RollbackController* rollback_{nullptr};
    QString activeVersion_{QStringLiteral("v2.0.0")};
    QString targetVersion_;
    QString carId_{QStringLiteral("car_1")};
    QStringList catalog_;
    bool simulating_{false};
    bool userDeployActive_{false};
    int simStep_{0};
};

}  // namespace sdv::dash::ota
