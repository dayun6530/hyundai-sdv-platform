#pragma once

#include "modules/ota/OtaTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace sdv::dash::ota {

class OtaDeploymentService;

class OtaDeploymentViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentState READ currentState NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString activeVersion READ activeVersion NOTIFY changed)
    Q_PROPERTY(QString targetVersion READ targetVersion NOTIFY changed)
    Q_PROPERTY(QStringList availableVersions READ availableVersions NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    explicit OtaDeploymentViewModel(OtaDeploymentService* service, QObject* parent = nullptr);

    QString currentState() const { return currentState_; }
    int progress() const { return progress_; }
    QString activeVersion() const { return activeVersion_; }
    QString targetVersion() const { return targetVersion_; }
    QStringList availableVersions() const { return availableVersions_; }
    bool busy() const { return busy_; }

    OtaDeploymentService* service() const { return service_; }

public slots:
    void deploySelectedVersion(const QString& version);
    void requestRollback();
    void resetDeployment();
    void ingestCarState(const QJsonObject& carState);

signals:
    void changed();
    void logLine(const QString& line);

private slots:
    void refreshFromService();
    void onLog(const QString& line);

private:
    void setCurrentState(const QString& state);
    void setProgress(int value);

    OtaDeploymentService* service_{nullptr};
    QString currentState_{QStringLiteral("Idle")};
    int progress_{0};
    QString activeVersion_;
    QString targetVersion_;
    QStringList availableVersions_;
    bool busy_{false};
};

}  // namespace sdv::dash::ota
