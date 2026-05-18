#include "MainWindow.h"

#include "ABTestingWidget.h"
#include "DashboardDataHub.h"
#include "EventTimelineWidget.h"
#include "FeatureStatusWidget.h"
#include "HealthMonitorWidget.h"
#include "OTADeploymentWidget.h"
#include "ShadowCompareWidget.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWidget>

using sdv::dash::MainWindow;

namespace {

QFrame* section(const QString& title, QWidget* inner, QWidget* parent) {
    auto* wrap = new QFrame(parent);
    wrap->setFrameShape(QFrame::StyledPanel);
    wrap->setStyleSheet(QStringLiteral(
        "QFrame { background-color:rgba(5,12,22,0.96);border:1px solid rgba(58,78,107,0.72);"
        "border-radius:8px; }"));
    auto* v = new QVBoxLayout(wrap);
    v->setContentsMargins(8, 6, 8, 8);
    v->setSpacing(5);
    auto* lab = new QLabel(
        QStringLiteral("<span style='color:#8ea6c7;font-size:10px;font-weight:700;letter-spacing:0.7px'>%1</span>")
            .arg(title.toUpper()));
    v->addWidget(lab);
    v->addWidget(inner);
    return wrap;
}

int otaProgressFromPhase(const QString& rawPhase, const QString& rawMode) {
    const QString phase = (rawPhase.isEmpty() ? rawMode : rawPhase).toUpper();
    if (phase.contains(QStringLiteral("DOWNLOAD"))) return 18;
    if (phase.contains(QStringLiteral("VERIFY")) || phase.contains(QStringLiteral("CHECKSUM"))) return 36;
    if (phase.contains(QStringLiteral("INSTALL"))) return 56;
    if (phase.contains(QStringLiteral("SWITCH"))) return 72;
    if (phase.contains(QStringLiteral("VALID"))) return 88;
    if (phase.contains(QStringLiteral("COMPLETE")) || phase.contains(QStringLiteral("READY"))) return 100;
    if (phase.contains(QStringLiteral("FAIL"))) return 100;
    return 0;
}

bool featureFlagOn(const QJsonObject& feature) {
    const QString flag = feature.value(QStringLiteral("flag")).toString();
    const bool hasRuntimeVersion = !feature.value(QStringLiteral("active_version")).toString().isEmpty();
    const bool enabled = flag.isEmpty() ? hasRuntimeVersion : flag.compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    return enabled && !feature.value(QStringLiteral("kill_switch")).toBool();
}

QJsonObject mqttFlagsForCar(const QJsonObject& carState, const QJsonObject& hubMqtt) {
    if (!hubMqtt.isEmpty()) {
        return hubMqtt;
    }
    return carState.value(QStringLiteral("mqtt_flags")).toObject();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), carId_(QStringLiteral("car_1")) {
    setWindowTitle(QStringLiteral("Hyundai SDV — Feature Operations Center"));
    resize(1600, 760);

    hub_ = new DashboardDataHub(this);
    hub_->setCarId(carId_);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    central->setStyleSheet(QStringLiteral(
        "QWidget { background-color:#030711; color:#dbe6f3; font-family:'Segoe UI'; font-size:10px; }"
        "QLabel { background:transparent; color:#d6e2f2; }"
        "QComboBox { background-color:#0d1624; color:#dbe6f3; border:1px solid #2c3d55;"
        "border-radius:8px; padding:4px 28px 4px 10px; }"
        "QScrollArea { background:#030711; border:0; }"
        "QScrollBar:vertical { background:#050b14; width:7px; margin:0; }"
        "QScrollBar::handle:vertical { background:#253752; border-radius:3px; min-height:28px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QProgressBar { background-color:#0a1321; border:1px solid #263852; border-radius:5px;"
        "height:10px; color:#dce7f7; text-align:center; font-size:9px; }"
        "QProgressBar::chunk { border-radius:5px; background-color:#38bdf8; }"
        "QTextEdit, QPlainTextEdit { background-color:#050b14; color:#cbd7e8; border:1px solid #22344d;"
        "border-radius:7px; selection-background-color:#0c4f95; }"
        "QSlider::groove:horizontal { height:6px; background:#101a2a; border-radius:3px; }"
        "QSlider::handle:horizontal { width:12px; margin:-4px 0; background:#38bdf8; border-radius:6px; }"
        "QPushButton { background-color:#0c2340; color:#dbeafe; border:1px solid #245db6;"
        "border-radius:7px; padding:5px 10px; font-size:10px; }"
        "QPushButton:hover { background-color:#12345f; }"));
    auto* rootLay = new QVBoxLayout(central);
    rootLay->setContentsMargins(4, 4, 4, 4);
    rootLay->setSpacing(4);

    carCombo_ = new QComboBox(central);
    for (int i = 1; i <= 10; ++i) {
        carCombo_->addItem(QStringLiteral("car_%1").arg(i));
    }
    carCombo_->setCurrentIndex(0);
    connect(carCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onCarComboChanged);
    carCombo_->hide();

    mqttLed_ = new QLabel(QStringLiteral("MQTT flags: offline"), central);
    mqttLed_->hide();

    auto* horizSplit = new QSplitter(Qt::Horizontal, central);
    horizSplit->setChildrenCollapsible(false);

    auto* leftSplit = new QSplitter(Qt::Vertical, horizSplit);
    leftSplit->setChildrenCollapsible(false);

    cockpit_ = new QWebEngineView(leftSplit);
    cockpit_->setContextMenuPolicy(Qt::NoContextMenu);
    cockpit_->setMinimumSize(980, 560);
    cockpit_->setUrl(QUrl(QStringLiteral("http://127.0.0.1:5175")));
    leftSplit->addWidget(cockpit_);

    timeline_ = new EventTimelineWidget(leftSplit);
    timeline_->setMinimumHeight(108);
    timeline_->setMaximumHeight(126);
    leftSplit->addWidget(timeline_);
    leftSplit->setStretchFactor(0, 12);
    leftSplit->setStretchFactor(1, 1);

    horizSplit->addWidget(leftSplit);
    horizSplit->setStretchFactor(0, 78);

    auto* rightScroll = new QScrollArea(horizSplit);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setMinimumWidth(340);
    rightScroll->setMaximumWidth(390);
    auto* rightInner = new QWidget();
    rightInner->setStyleSheet(QStringLiteral("QWidget { background:#030711; }"));
    auto* rv = new QVBoxLayout(rightInner);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(6);

    features_ = new FeatureStatusWidget(rightInner);
    ota_ = new OTADeploymentWidget(rightInner);
    ab_ = new ABTestingWidget(rightInner);
    shadow_ = new ShadowCompareWidget(rightInner);
    health_ = new HealthMonitorWidget(rightInner);

    ota_->setApiBase(hub_->apiBaseUrl());
    ota_->setCarId(carId_);

    rv->addWidget(section(QStringLiteral("OTA deploy control"), ota_, rightInner));
    rv->addWidget(section(QStringLiteral("System status"), features_, rightInner));
    rv->addWidget(section(QStringLiteral("A/B test"), ab_, rightInner));
    rv->addWidget(section(QStringLiteral("Shadow validation"), shadow_, rightInner));
    rv->addWidget(section(QStringLiteral("Health monitoring"), health_, rightInner));
    rv->addStretch();

    rightScroll->setWidget(rightInner);
    horizSplit->addWidget(rightScroll);
    horizSplit->setStretchFactor(1, 22);

    QList<int> hzSizes;
    hzSizes << 1220 << 360;
    horizSplit->setSizes(hzSizes);

    rootLay->addWidget(horizSplit, 1);

    connect(hub_, &DashboardDataHub::fleetStatesUpdated, this, &MainWindow::onFleetStates);
    connect(hub_, &DashboardDataHub::mqttFeaturesPayload, this, &MainWindow::onMqttFeatures);
    connect(hub_, &DashboardDataHub::timelineRecordsUpdated, this, &MainWindow::onTimeline);
    connect(hub_, &DashboardDataHub::httpLogLine, this, &MainWindow::onHttpLine);
    connect(hub_, &DashboardDataHub::mqttConnectivityChanged, this, [this](bool up) {
        mqttLed_->setText(up ? QStringLiteral("MQTT flags: connected") : QStringLiteral("MQTT flags: offline"));
        mqttLed_->setStyleSheet(up ? QStringLiteral("color:#34d399") : QStringLiteral("color:#f97316"));
        syncCockpitState(pickCar(lastFleet_, carId_));
    });

    connect(&trafficAnim_, &QTimer::timeout, this, &MainWindow::onTrafficAnimTick);
    trafficAnim_.start(140);
}

void MainWindow::applyLaunchOptions(const QString& apiBase,
                                    const QString& carId,
                                    const QString& mqttHost,
                                    int mqttPort) {
    hub_->setApiBaseUrl(apiBase);
    hub_->setMqttEndpoint(mqttHost, mqttPort);

    carId_ = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
    hub_->setCarId(carId_);

    {
        const QSignalBlocker blocker(carCombo_);
        const int idx = carCombo_->findText(carId_);
        if (idx >= 0) {
            carCombo_->setCurrentIndex(idx);
        }
    }

    ota_->setApiBase(apiBase);
    ota_->setCarId(carId_);
    syncCockpitState(QJsonObject{});
}

QJsonObject MainWindow::pickCar(const QJsonArray& states, const QString& carId) const {
    for (const auto& v : states) {
        const auto o = v.toObject();
        if (o.value(QStringLiteral("car_id")).toString() == carId) {
            return o;
        }
    }
    return {};
}

void MainWindow::syncCockpitState(const QJsonObject& carState) {
    if (!cockpit_) {
        return;
    }

    if (carState.isEmpty()) {
        return;
    }

    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
    const QJsonObject nav = feats.value(QStringLiteral("Navigation")).toObject();
    const QJsonObject energy = feats.value(QStringLiteral("Energy")).toObject();

    double speed = ota.value(QStringLiteral("speed")).toDouble(72.0);
    if (speed < 1.0) {
        speed = 72.0;
    }
    const int battery = qBound(0, qRound(ota.value(QStringLiteral("battery")).toDouble(82.0)), 100);

    const QString adasVersion = adas.value(QStringLiteral("active_version")).toString(QStringLiteral("v3.0"));
    const QString navVersion = nav.value(QStringLiteral("active_version")).toString(QStringLiteral("v2.1"));
    const QString evVersion = energy.value(QStringLiteral("active_version")).toString(QStringLiteral("v2.0"));
    const QString phase = ota.value(QStringLiteral("ota_phase")).toString();
    const QString mode = ota.value(QStringLiteral("update_mode")).toString();
    const bool upgraded = adasVersion.contains(QLatin1String("3")) ||
                          phase.contains(QLatin1String("COMPLETE"), Qt::CaseInsensitive) ||
                          mode.contains(QLatin1String("READY"), Qt::CaseInsensitive);

    const QJsonObject mqttFeatures = lastMqtt_.value(QStringLiteral("features")).toObject();
    const QJsonObject adasM = mqttFeatures.value(QStringLiteral("AdasFeature")).toObject();
    const QJsonObject navM = mqttFeatures.value(QStringLiteral("NavigationFeature")).toObject();
    const QJsonObject evM = mqttFeatures.value(QStringLiteral("EvEnergyFeature")).toObject();

    const bool adasRunning = adasM.isEmpty() ? featureFlagOn(adas) : adasM.value(QStringLiteral("main")).toBool(featureFlagOn(adas));
    const bool navRunning = navM.isEmpty() ? featureFlagOn(nav) : navM.value(QStringLiteral("main")).toBool(featureFlagOn(nav));
    const bool evRunning = evM.isEmpty() ? featureFlagOn(energy) : evM.value(QStringLiteral("main")).toBool(featureFlagOn(energy));

    Q_UNUSED(speed);
    Q_UNUSED(battery);
    Q_UNUSED(adasVersion);
    Q_UNUSED(navVersion);
    Q_UNUSED(evVersion);
    Q_UNUSED(adasRunning);
    Q_UNUSED(navRunning);
    Q_UNUSED(evRunning);
    Q_UNUSED(upgraded);
    Q_UNUSED(phase);
    Q_UNUSED(mode);
}

void MainWindow::onFleetStates(const QJsonArray& states) {
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

void MainWindow::onMqttFeatures(const QJsonObject& env) {
    lastMqtt_ = env;
    const QJsonObject car = pickCar(lastFleet_, carId_);
    if (!car.isEmpty()) {
        syncCockpitState(car);
        features_->updateCards(car, mqttFlagsForCar(car, lastMqtt_));
    }
}

void MainWindow::onTimeline(const QJsonArray& records) {
    timeline_->setFleetHistory(records);
}

void MainWindow::onHttpLine(const QString& line) {
    timeline_->appendLine(line);
}

void MainWindow::onCarComboChanged(int idx) {
    Q_UNUSED(idx);
    carId_ = carCombo_->currentText();
    hub_->setCarId(carId_);
    ota_->setCarId(carId_);
    const QJsonObject car = pickCar(lastFleet_, carId_);
    syncCockpitState(car);
    if (!car.isEmpty()) {
        const QJsonObject mqtt = mqttFlagsForCar(car, lastMqtt_);
        features_->updateCards(car, mqtt);
        ota_->updateFromCarState(car);
        shadow_->updateFromCarState(car);
        health_->updateFromCarState(car);
    }
}

void MainWindow::onCockpitCarSelected(const QString& carId) {
    const QString next = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
    const int idx = carCombo_->findText(next);
    if (idx >= 0 && idx != carCombo_->currentIndex()) {
        carCombo_->setCurrentIndex(idx);
        return;
    }

    carId_ = next;
    hub_->setCarId(carId_);
    ota_->setCarId(carId_);
    const QJsonObject car = pickCar(lastFleet_, carId_);
    syncCockpitState(car);
    if (!car.isEmpty()) {
        const QJsonObject mqtt = mqttFlagsForCar(car, lastMqtt_);
        features_->updateCards(car, mqtt);
        ota_->updateFromCarState(car);
        shadow_->updateFromCarState(car);
        health_->updateFromCarState(car);
    }
}

void MainWindow::onTrafficAnimTick() {
    ab_->tickTrafficShift();
}
