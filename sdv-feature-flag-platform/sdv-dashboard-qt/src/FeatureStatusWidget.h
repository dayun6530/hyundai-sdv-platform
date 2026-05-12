#pragma once

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class QLabel;
class QFrame;

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
        QLabel* ver{nullptr};
        QLabel* state{nullptr};
        QLabel* hb{nullptr};
        QLabel* cpu{nullptr};
        QLabel* mem{nullptr};
        QFrame* frame{nullptr};
    };
    void styleCard(QFrame* f, const QString& accent);
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
