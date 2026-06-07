#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <QTimer>

#include "ui/NavSidebar.h"

class QScrollArea;
class QSplitter;

namespace sdv::dash::mapbox {
class MapboxBridge;
}

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::ota {
class OtaDeploymentService;
class OtaDeploymentViewModel;
}

namespace sdv::dash::carla::integration {
class CarlaIntegrationController;
}

namespace sdv::dash::carla::control {
class CarlaControlManager;
}

namespace sdv::dash::twin {
class DigitalTwinController;
}

namespace sdv::dash::ui {
class CockpitPanel;
class TopBar;
class GlassPanel;
}

namespace sdv::dash {

class DashboardDataHub;
class FeatureStatusWidget;
class OTADeploymentWidget;
class ABTestingWidget;
class ShadowCompareWidget;
class HealthMonitorWidget;
class EventTimelineWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void applyLaunchOptions(const QString& apiBase,
                            const QString& carId,
                            const QString& mqttHost,
                            int mqttPort,
                            const QString& rosWsUrl = QStringLiteral("ws://127.0.0.1:9090"),
                            const QString& mapboxToken = QString(),
                            const QString& carlaMode = QStringLiteral("off"));

    void selectVehicle(const QString& carId);

private slots:
    void onFleetStates(const QJsonArray& states);
    void onMqttFeatures(const QJsonObject& env);
    void onTimeline(const QJsonArray& records);
    void onHttpLine(const QString& line);
    void onCockpitCarSelected(const QString& carId);
    void onTrafficAnimTick();
    void onNavSectionSelected(sdv::dash::ui::NavSidebar::Section section);

private:
    QJsonObject pickCar(const QJsonArray& states, const QString& carId) const;
    void syncCockpitState(const QJsonObject& carState);
    void scrollRightPanelTo(QWidget* widget);
    void applyImmersivePygameChrome(bool enabled);

    sdv::dash::ops::OpsTelemetryPipeline* opsPipeline_{nullptr};
    sdv::dash::carla::integration::CarlaIntegrationController* carlaIntegration_{nullptr};
    sdv::dash::carla::control::CarlaControlManager* carlaControl_{nullptr};
    sdv::dash::twin::DigitalTwinController* digitalTwin_{nullptr};
    sdv::dash::mapbox::MapboxBridge* mapboxBridge_{nullptr};
    DashboardDataHub* hub_{nullptr};
    bool carlaBridgePygameCockpit_{false};

    sdv::dash::ui::TopBar* topBar_{nullptr};
    sdv::dash::ui::NavSidebar* navSidebar_{nullptr};
    sdv::dash::ui::CockpitPanel* cockpitPanel_{nullptr};

    FeatureStatusWidget* features_{nullptr};
    OTADeploymentWidget* ota_{nullptr};
    sdv::dash::ota::OtaDeploymentService* otaService_{nullptr};
    sdv::dash::ota::OtaDeploymentViewModel* otaViewModel_{nullptr};
    ABTestingWidget* ab_{nullptr};
    ShadowCompareWidget* shadow_{nullptr};
    HealthMonitorWidget* health_{nullptr};
    EventTimelineWidget* timeline_{nullptr};

    QWidget* opsPanel_{nullptr};
    sdv::dash::ui::GlassPanel* featuresPanel_{nullptr};
    sdv::dash::ui::GlassPanel* otaPanel_{nullptr};
    sdv::dash::ui::GlassPanel* abPanel_{nullptr};
    sdv::dash::ui::GlassPanel* shadowPanel_{nullptr};
    sdv::dash::ui::GlassPanel* healthPanel_{nullptr};
    sdv::dash::ui::GlassPanel* settingsPanel_{nullptr};
    QScrollArea* rightScroll_{nullptr};
    QSplitter* bodySplit_{nullptr};

    QJsonArray lastFleet_;
    QJsonObject lastMqtt_;
    QString carId_;
    bool mqttUp_{false};
    QTimer trafficAnim_;
};

}  // namespace sdv::dash
