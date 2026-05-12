#pragma once

#include <QJsonObject>
#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QNetworkAccessManager;

namespace sdv::dash {

class OTADeploymentWidget : public QWidget {
    Q_OBJECT
public:
    explicit OTADeploymentWidget(QWidget* parent = nullptr);
    void setApiBase(const QString& url);
    void setCarId(const QString& carId);
    void updateFromCarState(const QJsonObject& carState);

private slots:
    void onRollback();

private:
    void appendLog(const QString& line);
    void restageProgress(const QJsonObject& ota);

    QNetworkAccessManager* nam_;
    QString apiBase_;
    QString carId_;

    QLabel* activeVer_{nullptr};
    QLabel* targetVer_{nullptr};
    QProgressBar* progress_{nullptr};
    QLabel* stages_{nullptr};
    QTextEdit* log_{nullptr};
    QPushButton* rollback_{nullptr};
    QString lastOtaSig_;
};

}  // namespace sdv::dash
