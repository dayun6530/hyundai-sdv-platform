#pragma once

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;

namespace sdv::dash::ui {
class OtaFlowDiagramWidget;
}

namespace sdv::dash::ota {
class OtaDeploymentViewModel;
}

namespace sdv::dash {

class OTADeploymentWidget : public QWidget {
    Q_OBJECT
public:
    explicit OTADeploymentWidget(QWidget* parent = nullptr);

    void setViewModel(sdv::dash::ota::OtaDeploymentViewModel* viewModel);
    void setApiBase(const QString& url);
    void setCarId(const QString& carId);
    void updateFromCarState(const QJsonObject& carState);

private slots:
    void onDeploy();
    void onRollback();
    void onViewModelChanged();
    void onLogLine(const QString& line);

private:
    void rebuildTimeline();
    void appendLog(const QString& line);
    void updateProgressStyle();

    sdv::dash::ota::OtaDeploymentViewModel* viewModel_{nullptr};

    QComboBox* versionSelector_{nullptr};
    QPushButton* deployBtn_{nullptr};
    QLabel* activeVer_{nullptr};
    QLabel* targetVer_{nullptr};
    QLabel* stateLabel_{nullptr};
    QProgressBar* progress_{nullptr};
    sdv::dash::ui::OtaFlowDiagramWidget* flowDiagram_{nullptr};
    QTextEdit* deploymentLog_{nullptr};
    QPushButton* rollback_{nullptr};

    QString lastState_;
};

}  // namespace sdv::dash
