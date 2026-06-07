#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace sdv::dash::ota {

class OtaStateMachine;

class RollbackController : public QObject {
    Q_OBJECT
public:
    explicit RollbackController(OtaStateMachine* stateMachine, QObject* parent = nullptr);

    void setApiBase(const QString& url);
    void setCarId(const QString& carId);
    void setFeatureName(const QString& feature) { featureName_ = feature; }

    bool isActive() const { return active_; }

public slots:
    void requestRollback(const QString& activeVersion);

signals:
    void rollbackStarted();
    void rollbackFinished(bool success, const QString& detail);
    void logLine(const QString& line);

private slots:
    void onHttpFinished(QNetworkReply* reply);
    void onSimStep();

private:
    void finishLocal(bool success, const QString& detail);
    void startSimulation();

    OtaStateMachine* stateMachine_{nullptr};
    QNetworkAccessManager* nam_{nullptr};
    QString apiBase_;
    QString carId_;
    QString featureName_{QStringLiteral("ADAS")};
    QString restoreVersion_;
    bool active_{false};
    bool useSimulation_{true};
    int simTicks_{0};
};

}  // namespace sdv::dash::ota
