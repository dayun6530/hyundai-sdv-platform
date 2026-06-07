#include "MainWindow.h"

#include "ABTestingWidget.h"
#include "DashboardDataHub.h"
#include "EventTimelineWidget.h"
#include "FeatureStatusWidget.h"
#include "HealthMonitorWidget.h"
#include "OTADeploymentWidget.h"
#include "ShadowCompareWidget.h"
#include "examples/OpsIngestIntegrationExample.h"
#include "mapbox/MapboxBridge.h"
#include "mapbox/MapboxTokenLoader.h"
#include "mapbox/MapboxCockpitView.h"
#include "modules/ota/OtaDeploymentService.h"
#include "modules/ota/OtaDeploymentViewModel.h"
#include "carla/CarlaCockpitView.h"
#include "carla/integration/CarlaDataBridge.h"
#include "carla/integration/CarlaIntegrationController.h"
#include "carla/control/CarlaControlManager.h"
#include "carla/integration/CarlaDataBridge.h"
#include "twin/DigitalTwinController.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ui/CockpitHudOverlay.h"
#include "ui/CockpitPanel.h"
#include "ui/DashboardTheme.h"
#include "ui/GlassPanel.h"
#include "ui/NavSidebar.h"
#include "ui/TopBar.h"

#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

using sdv::dash::MainWindow;

namespace {

QJsonObject mqttFlagsForCar(const QJsonObject& carState, const QJsonObject& hubMqtt)
{
    if (!hubMqtt.isEmpty()) {
        return hubMqtt;
    }
    return carState.value(QStringLiteral("mqtt_flags")).toObject();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), carId_(QStringLiteral("car_1"))
{
    setWindowTitle(QStringLiteral("Hyundai SDV — Feature Operations Center"));
    resize(1680, 920);

    hub_ = new DashboardDataHub(this);
    hub_->setCarId(carId_);

    opsPipeline_ = new sdv::dash::ops::OpsTelemetryPipeline(this);
    carlaIntegration_ = new sdv::dash::carla::integration::CarlaIntegrationController(this);
    carlaControl_ = new sdv::dash::carla::control::CarlaControlManager(this);
    digitalTwin_ = new sdv::dash::twin::DigitalTwinController(this);
    mapboxBridge_ = new sdv::dash::mapbox::MapboxBridge(this);
    {
        const auto tokenInfo = sdv::dash::mapbox::resolveMapboxToken();
        if (!tokenInfo.token.isEmpty()) {
            mapboxBridge_->setMapboxToken(tokenInfo.token);
        } else if (!tokenInfo.warning.isEmpty()) {
            qWarning().noquote() << tokenInfo.warning;
        }
    }

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("sdvRoot"));
    setCentralWidget(root);
    root->setStyleSheet(sdv::dash::ui::DashboardTheme::applicationStyleSheet());

    auto* rootLay = new QVBoxLayout(root);
    rootLay->setContentsMargins(10, 10, 10, 10);
    rootLay->setSpacing(8);

    topBar_ = new sdv::dash::ui::TopBar(root);
    rootLay->addWidget(topBar_);

    auto* bodySplit = new QSplitter(Qt::Horizontal, root);
    bodySplit_ = bodySplit;
    bodySplit->setChildrenCollapsible(false);

    navSidebar_ = new sdv::dash::ui::NavSidebar(bodySplit);
    bodySplit->addWidget(navSidebar_);

    auto* centerColumn = new QWidget(bodySplit);
    auto* centerLay = new QVBoxLayout(centerColumn);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(8);

    cockpitPanel_ = new sdv::dash::ui::CockpitPanel(centerColumn);
    cockpitPanel_->setMapboxBridge(mapboxBridge_);
    cockpitPanel_->bindPipeline(opsPipeline_);
    mapboxBridge_->setCameraMode(QStringLiteral("first_person"));
    centerLay->addWidget(cockpitPanel_, 1);

    timeline_ = new EventTimelineWidget(centerColumn);
    timeline_->setMinimumHeight(140);
    timeline_->setMaximumHeight(190);
    centerLay->addWidget(timeline_, 0);

    bodySplit->addWidget(centerColumn);
    bodySplit->setStretchFactor(1, 1);

    rightScroll_ = new QScrollArea(bodySplit);
    rightScroll_->setWidgetResizable(true);
    rightScroll_->setFrameShape(QFrame::NoFrame);
    rightScroll_->setMinimumWidth(320);
    rightScroll_->setMaximumWidth(400);

    auto* rightInner = new QWidget();
    rightInner->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* rv = new QVBoxLayout(rightInner);
    rv->setContentsMargins(0, 0, 4, 0);
    rv->setSpacing(10);

    features_ = new FeatureStatusWidget(rightInner);
    featuresPanel_ = new sdv::dash::ui::GlassPanel(QStringLiteral("System Status"), rightInner);
    featuresPanel_->setContentWidget(features_);
    rv->addWidget(featuresPanel_);

    otaService_ = new sdv::dash::ota::OtaDeploymentService(this);
    otaViewModel_ = new sdv::dash::ota::OtaDeploymentViewModel(otaService_, this);
    ota_ = new OTADeploymentWidget(rightInner);
    ota_->setViewModel(otaViewModel_);
    otaPanel_ = new sdv::dash::ui::GlassPanel(QStringLiteral("OTA Deployment"), rightInner);
    otaPanel_->setContentWidget(ota_);
    rv->addWidget(otaPanel_);

    ab_ = new ABTestingWidget(rightInner);
    abPanel_ = new sdv::dash::ui::GlassPanel(QStringLiteral("A/B Testing"), rightInner);
    abPanel_->setContentWidget(ab_);
    rv->addWidget(abPanel_);

    shadow_ = new ShadowCompareWidget(rightInner);
    shadowPanel_ = new sdv::dash::ui::GlassPanel(QStringLiteral("Shadow Validation"), rightInner);
    shadowPanel_->setContentWidget(shadow_);
    rv->addWidget(shadowPanel_);

    health_ = new HealthMonitorWidget(rightInner);
    healthPanel_ = new sdv::dash::ui::GlassPanel(QStringLiteral("Health Monitoring"), rightInner);
    healthPanel_->setContentWidget(health_);
    rv->addWidget(healthPanel_);

    opsPanel_ = new sdv::dash::ops::OpsIngestIntegrationPanel(opsPipeline_, rightInner);
    settingsPanel_ = new sdv::dash::ui::GlassPanel(QStringLiteral("Settings & ROS Telemetry"), rightInner);
    settingsPanel_->setContentWidget(opsPanel_);
    settingsPanel_->hide();
    rv->addWidget(settingsPanel_);

    rv->addStretch();
    rightScroll_->setWidget(rightInner);
    bodySplit->addWidget(rightScroll_);

    bodySplit->setSizes({92, 1180, 360});
    rootLay->addWidget(bodySplit, 1);

    connect(cockpitPanel_, &sdv::dash::ui::CockpitPanel::vehicleSelected, this, &MainWindow::onCockpitCarSelected);
    connect(cockpitPanel_, &sdv::dash::ui::CockpitPanel::cameraFramePresented, health_, &HealthMonitorWidget::noteCameraFrame);
    connect(navSidebar_, &sdv::dash::ui::NavSidebar::sectionSelected, this, &MainWindow::onNavSectionSelected);
    connect(topBar_, &sdv::dash::ui::TopBar::viewModeChanged, this, [this](const QString& mode) {
        if (mapboxBridge_) {
            mapboxBridge_->setCameraMode(mode);
        }
        if (cockpitPanel_) {
            const bool fullMap = mode.contains(QStringLiteral("Operations"), Qt::CaseInsensitive) ||
                                 mode.contains(QStringLiteral("Dashboard"), Qt::CaseInsensitive);
            cockpitPanel_->setFullMapVisible(fullMap);
            if (carlaBridgePygameCockpit_) {
                cockpitPanel_->setPygameMapCockpitMode(!fullMap);
                cockpitPanel_->setDriverViewChrome(false);
            } else {
                const bool driverChrome =
                    mode.contains(QStringLiteral("1st")) || mode.contains(QStringLiteral("3rd"));
                cockpitPanel_->setDriverViewChrome(driverChrome);
            }
        }
    });
    cockpitPanel_->setDriverViewChrome(true);
    cockpitPanel_->setPygameMapCockpitMode(false);

    connect(opsPipeline_, &sdv::dash::ops::OpsTelemetryPipeline::statusLineChanged, this, [this](const QString& line) {
        if (health_) {
            health_->setRosTelemetryLine(line);
        }
    });

    connect(hub_, &DashboardDataHub::fleetStatesUpdated, this, &MainWindow::onFleetStates);
    connect(hub_, &DashboardDataHub::mqttFeaturesPayload, this, &MainWindow::onMqttFeatures);
    connect(hub_, &DashboardDataHub::timelineRecordsUpdated, this, &MainWindow::onTimeline);
    connect(hub_, &DashboardDataHub::httpLogLine, this, &MainWindow::onHttpLine);
    connect(hub_, &DashboardDataHub::mqttConnectivityChanged, this, [this](bool up) {
        mqttUp_ = up;
        syncCockpitState(pickCar(lastFleet_, carId_));
    });

    connect(&trafficAnim_, &QTimer::timeout, this, &MainWindow::onTrafficAnimTick);
    trafficAnim_.start(140);

    const QSignalBlocker blocker(cockpitPanel_->hud()->vehicleSelector());
    cockpitPanel_->hud()->vehicleSelector()->setCurrentIndex(0);
}

void MainWindow::applyLaunchOptions(const QString& apiBase,
                                    const QString& carId,
                                    const QString& mqttHost,
                                    int mqttPort,
                                    const QString& rosWsUrl,
                                    const QString& mapboxToken,
                                    const QString& carlaMode)
{
    hub_->setApiBaseUrl(apiBase);
    hub_->setMqttEndpoint(mqttHost, mqttPort);

    if (mapboxBridge_) {
        const auto tokenInfo = sdv::dash::mapbox::resolveMapboxToken(mapboxToken);
        if (!tokenInfo.token.isEmpty()) {
            mapboxBridge_->setMapboxToken(tokenInfo.token);
        } else if (!tokenInfo.warning.isEmpty()) {
            qWarning().noquote() << tokenInfo.warning;
        }
    }

    carId_ = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
    hub_->setCarId(carId_);

    const int idx = cockpitPanel_->hud()->vehicleSelector()->findText(carId_);
    if (idx >= 0) {
        const QSignalBlocker blocker(cockpitPanel_->hud()->vehicleSelector());
        cockpitPanel_->hud()->vehicleSelector()->setCurrentIndex(idx);
    }

    ota_->setApiBase(apiBase);
    ota_->setCarId(carId_);
    syncCockpitState(QJsonObject{});

    if (cockpitPanel_) {
        cockpitPanel_->setVehicleId(carId_);
    }

    using CarlaMode = sdv::dash::carla::integration::CarlaIntegrationController::Mode;
    const QString modeKey = carlaMode.trimmed().toLower();
    CarlaMode integrationMode = CarlaMode::Off;
    if (modeKey == QLatin1String("sim") || modeKey == QLatin1String("native-sim")) {
        integrationMode = CarlaMode::NativeSim;
    } else if (modeKey == QLatin1String("carla-ws") || modeKey == QLatin1String("native-carla-ws")) {
        integrationMode = CarlaMode::NativeCarlaWs;
    } else if (modeKey == QLatin1String("bridge") || modeKey == QLatin1String("external")) {
        integrationMode = CarlaMode::ExternalWsBridge;
    }

    bool useRosBridge = true;
    if (carlaIntegration_ && integrationMode != CarlaMode::Off) {
        carlaIntegration_->configure(
            integrationMode,
            opsPipeline_,
            cockpitPanel_ ? cockpitPanel_->carlaView() : nullptr);
        if (integrationMode == CarlaMode::NativeSim || integrationMode == CarlaMode::NativeCarlaWs) {
            carlaIntegration_->start();
            useRosBridge = false;
        }
    }

    if (useRosBridge && opsPipeline_) {
        const QString trimmed = rosWsUrl.trimmed();
        if (!trimmed.isEmpty() && trimmed.compare(QLatin1String("off"), Qt::CaseInsensitive) != 0) {
            const QUrl ws(trimmed);
            opsPipeline_->start(ws);
            if (cockpitPanel_) {
                cockpitPanel_->setTelemetryWebSocketUrl(ws);
            }
        }
    }

    if (carlaControl_ && cockpitPanel_) {
        sdv::dash::carla::integration::CarlaSimAdapter* simAdapter = nullptr;
        if (carlaIntegration_ && carlaIntegration_->dataBridge()) {
            simAdapter = carlaIntegration_->dataBridge()->simulatorAdapter();
        }
        carlaControl_->configure(
            carlaMode,
            cockpitPanel_->carlaView(),
            cockpitPanel_->carlaView()->hudOverlay(),
            simAdapter,
            QUrl(QStringLiteral("ws://127.0.0.1:9093")),
            opsPipeline_);
        if (integrationMode == CarlaMode::ExternalWsBridge) {
            carlaBridgePygameCockpit_ = true;
            if (mapboxBridge_) {
                mapboxBridge_->setCameraMode(QStringLiteral("first_person"));
            }
            if (cockpitPanel_) {
                cockpitPanel_->setPygameMapCockpitMode(true);
                cockpitPanel_->setFullMapVisible(false);
                cockpitPanel_->setDriverViewChrome(false);
            }
            applyImmersivePygameChrome(true);
        } else {
            carlaBridgePygameCockpit_ = false;
            applyImmersivePygameChrome(false);
        }
        connect(carlaControl_, &sdv::dash::carla::control::CarlaControlManager::statusMessage, this,
                [this](const QString& line) {
                    if (health_) {
                        health_->setRosTelemetryLine(line);
                    }
                });
        if (cockpitPanel_->carlaView()) {
            cockpitPanel_->carlaView()->setFocus();
        }
    }

    if (digitalTwin_ && cockpitPanel_) {
        sdv::dash::carla::integration::CarlaDataBridge* dataBridge =
            carlaIntegration_ ? carlaIntegration_->dataBridge() : nullptr;
        digitalTwin_->configure(carlaMode, dataBridge, carlaControl_, mapboxBridge_,
                                cockpitPanel_->carlaView(), opsPipeline_);
        connect(digitalTwin_, &sdv::dash::twin::DigitalTwinController::statusMessage, this,
                [this](const QString& line) {
                    if (health_) {
                        health_->setRosTelemetryLine(line);
                    }
                });
    }
}

void MainWindow::applyImmersivePygameChrome(const bool enabled)
{
    for (QWidget* chrome : {static_cast<QWidget*>(topBar_),
                            static_cast<QWidget*>(navSidebar_),
                            static_cast<QWidget*>(rightScroll_),
                            static_cast<QWidget*>(timeline_)}) {
        if (chrome) {
            chrome->setVisible(!enabled);
        }
    }
    if (auto* root = centralWidget()) {
        if (auto* lay = qobject_cast<QVBoxLayout*>(root->layout())) {
            const int margin = enabled ? 0 : 10;
            lay->setContentsMargins(margin, margin, margin, margin);
            lay->setSpacing(enabled ? 0 : 8);
        }
    }
    if (bodySplit_ && enabled) {
        bodySplit_->setSizes({0, 10000, 0});
    }
}

QJsonObject MainWindow::pickCar(const QJsonArray& states, const QString& carId) const
{
    for (const auto& v : states) {
        const auto o = v.toObject();
        if (o.value(QStringLiteral("car_id")).toString() == carId) {
            return o;
        }
    }
    return {};
}

void MainWindow::syncCockpitState(const QJsonObject& carState)
{
    if (!cockpitPanel_) {
        return;
    }
    cockpitPanel_->updateFromCarState(carState, mqttUp_);
}

void MainWindow::scrollRightPanelTo(QWidget* widget)
{
    if (!rightScroll_ || !widget) {
        return;
    }
    rightScroll_->ensureWidgetVisible(widget, 24);
}

void MainWindow::onNavSectionSelected(const sdv::dash::ui::NavSidebar::Section section)
{
    featuresPanel_->show();
    otaPanel_->show();
    abPanel_->show();
    shadowPanel_->show();
    healthPanel_->show();
    settingsPanel_->hide();

    switch (section) {
    case sdv::dash::ui::NavSidebar::Section::Deployment:
        scrollRightPanelTo(otaPanel_);
        break;
    case sdv::dash::ui::NavSidebar::Section::AbTest:
        scrollRightPanelTo(abPanel_);
        break;
    case sdv::dash::ui::NavSidebar::Section::Shadow:
        scrollRightPanelTo(shadowPanel_);
        break;
    case sdv::dash::ui::NavSidebar::Section::Settings:
        settingsPanel_->show();
        scrollRightPanelTo(settingsPanel_);
        break;
    case sdv::dash::ui::NavSidebar::Section::Adas:
    case sdv::dash::ui::NavSidebar::Section::Navigation:
    case sdv::dash::ui::NavSidebar::Section::Ev:
    case sdv::dash::ui::NavSidebar::Section::Vehicle:
        scrollRightPanelTo(featuresPanel_);
        break;
    case sdv::dash::ui::NavSidebar::Section::Home:
    default:
        scrollRightPanelTo(otaPanel_);
        break;
    }
}

void MainWindow::onFleetStates(const QJsonArray& states)
{
    lastFleet_ = states;
    ab_->updateRollup(states);

    const QJsonObject car = pickCar(states, carId_);
    if (!car.isEmpty()) {
        const QJsonObject mqtt = mqttFlagsForCar(car, lastMqtt_);
        syncCockpitState(car);
        features_->updateCards(car, mqtt);
        ota_->updateFromCarState(car);
        shadow_->updateFromCarState(car);
        health_->updateFromCarState(car);
    }
}

void MainWindow::onMqttFeatures(const QJsonObject& env)
{
    lastMqtt_ = env;
    const QJsonObject car = pickCar(lastFleet_, carId_);
    if (!car.isEmpty()) {
        syncCockpitState(car);
        features_->updateCards(car, mqttFlagsForCar(car, lastMqtt_));
    }
}

void MainWindow::onTimeline(const QJsonArray& records)
{
    timeline_->setFleetHistory(records);
}

void MainWindow::onHttpLine(const QString& line)
{
    timeline_->appendLine(line);
}

void MainWindow::selectVehicle(const QString& carId)
{
    const QString next = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
    const int idx = cockpitPanel_->hud()->vehicleSelector()->findText(next);
    if (idx >= 0) {
        const QSignalBlocker blocker(cockpitPanel_->hud()->vehicleSelector());
        cockpitPanel_->hud()->vehicleSelector()->setCurrentIndex(idx);
    } else {
        carId_ = next;
        hub_->setCarId(carId_);
        ota_->setCarId(carId_);
        const QJsonObject car = pickCar(lastFleet_, carId_);
        syncCockpitState(car);
        if (!car.isEmpty()) {
            features_->updateCards(car, mqttFlagsForCar(car, lastMqtt_));
            ota_->updateFromCarState(car);
            shadow_->updateFromCarState(car);
            health_->updateFromCarState(car);
        }
    }
    raise();
    activateWindow();
}

void MainWindow::onCockpitCarSelected(const QString& carId)
{
    carId_ = carId;
    hub_->setCarId(carId_);
    ota_->setCarId(carId_);
    const QJsonObject car = pickCar(lastFleet_, carId_);
    syncCockpitState(car);
    if (!car.isEmpty()) {
        features_->updateCards(car, mqttFlagsForCar(car, lastMqtt_));
        ota_->updateFromCarState(car);
        shadow_->updateFromCarState(car);
        health_->updateFromCarState(car);
    }
}

void MainWindow::onTrafficAnimTick()
{
    ab_->tickTrafficShift();
}
