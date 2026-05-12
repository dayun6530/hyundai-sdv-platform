#pragma once

#include <QGraphicsView>
#include <QJsonObject>
#include <QTimer>

class QGraphicsScene;
class QPropertyAnimation;
class QResizeEvent;

namespace sdv::dash {

class VehicleHUDView : public QGraphicsView {
    Q_OBJECT
    Q_PROPERTY(qreal upgradeGlow READ upgradeGlow WRITE setUpgradeGlow)
public:
    explicit VehicleHUDView(QWidget* parent = nullptr);
    void ingestFleetCar(const QJsonObject& carState);
    void ingestMqttFlags(const QJsonObject& mqttEnvelope);
    qreal upgradeGlow() const { return upgradeGlow_; }
    void setUpgradeGlow(qreal value);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTick();

private:
    QGraphicsScene* scene_{nullptr};
    QPropertyAnimation* glowAnim_{nullptr};
    QTimer timer_;
    QJsonObject car_;
    QJsonObject mqtt_;

    qreal simTime_{0};
    qreal roadPhase_{0};
    qreal upgradeGlow_{0};
};

}  // namespace sdv::dash
