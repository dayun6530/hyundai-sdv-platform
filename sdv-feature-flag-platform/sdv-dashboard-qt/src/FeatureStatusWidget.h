#pragma once

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class QLabel;
class QFrame;
class QProgressBar;

namespace sdv::dash {

class FeatureStatusWidget : public QWidget {
    Q_OBJECT
public:
    explicit FeatureStatusWidget(QWidget* parent = nullptr);
    void updateCards(const QJsonObject& carState, const QJsonObject& mqttFeatures);

private slots:
    void pulseHeartbeat();

private:
    struct CardUi {
        QLabel* title{nullptr};
        QLabel* badge{nullptr};
        QLabel* ver{nullptr};
        QLabel* health{nullptr};
        QProgressBar* cpuBar{nullptr};
        QProgressBar* memBar{nullptr};
        QLabel* cpuLbl{nullptr};
        QLabel* memLbl{nullptr};
        QFrame* frame{nullptr};
    };
    void styleCard(QFrame* f, const QString& accentRgb);
    void fillCard(CardUi& c, const QString& displayName, const QJsonObject& fleetFeat,
                  const QJsonObject& mqttFeat, const QJsonObject& ota);

    CardUi adas_;
    CardUi nav_;
    CardUi energy_;
    QTimer hbTimer_;
    bool hbOn_{false};
    QJsonObject lastCar_;
    QJsonObject lastMqtt_;
};

}  // namespace sdv::dash
