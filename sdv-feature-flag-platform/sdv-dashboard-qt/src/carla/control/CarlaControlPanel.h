#pragma once



#include <QWidget>



class QComboBox;

class QPushButton;

class QLabel;

class QDoubleSpinBox;



namespace sdv::dash::carla::control {



class CarlaControlManager;



/** Driving console chrome: camera, autopilot, e-stop, destination, mode badge. */

class CarlaControlPanel : public QWidget {

    Q_OBJECT

public:

    explicit CarlaControlPanel(QWidget* parent = nullptr);



    void bindManager(CarlaControlManager* manager);



protected:

    void paintEvent(QPaintEvent* event) override;



private:

    void onCameraChanged(int index);

    void onAutopilotClicked();

    void onEmergencyClicked();

    void onRouteClicked();

    void onStartAutoClicked();

    void refreshAgentUi();



    CarlaControlManager* manager_{nullptr};

    QLabel* modeBadge_{nullptr};

    QLabel* destHint_{nullptr};

    QLabel* agentStatus_{nullptr};

    QComboBox* agentModeBox_{nullptr};

    QComboBox* cameraBox_{nullptr};

    QPushButton* autopilotBtn_{nullptr};

    QPushButton* emergencyBtn_{nullptr};

    QPushButton* startAutoBtn_{nullptr};

    QDoubleSpinBox* destX_{nullptr};

    QDoubleSpinBox* destY_{nullptr};

};



}  // namespace sdv::dash::carla::control

