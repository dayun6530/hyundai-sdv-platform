#include "ui/CockpitPanel.h"



#include "carla/CarlaCockpitView.h"

#include "mapbox/MapboxBridge.h"

#include "mapbox/MapboxCockpitView.h"

#include "mapbox/MiniMapView.h"

#include "ops/OpsTelemetryPipeline.h"

#include "ui/CockpitHudOverlay.h"

#include "ui/CockpitInstrumentDock.h"

#include "ui/CockpitMapHud.h"

#include "ui/DashboardTheme.h"



#include <QGraphicsDropShadowEffect>

#include <QGridLayout>

#include <QJsonObject>

#include <QLabel>

#include <QVBoxLayout>



namespace sdv::dash::ui {



CockpitPanel::CockpitPanel(QWidget* parent) : QWidget(parent)

{

    auto* outer = new QVBoxLayout(this);

    outer->setContentsMargins(0, 0, 0, 0);

    outer->setSpacing(8);



    auto* frame = new QFrame(this);

    frame->setObjectName(QStringLiteral("cockpitFrame"));

    frame->setStyleSheet(DashboardTheme::cockpitFrameStyle());

    auto* shadow = new QGraphicsDropShadowEffect(frame);

    shadow->setBlurRadius(32);

    shadow->setOffset(0, 8);

    shadow->setColor(QColor(0, 160, 255, 48));

    frame->setGraphicsEffect(shadow);



    auto* stack = new QGridLayout(frame);

    stack->setContentsMargins(0, 0, 0, 0);

    stack->setSpacing(0);



    carlaView_ = new sdv::dash::carla::CarlaCockpitView(frame);

    mapboxView_ = new sdv::dash::mapbox::MapboxCockpitView(frame);

    mapboxView_->hide();

    carlaView_->setMinimumHeight(420);

    carlaView_->setFocusPolicy(Qt::StrongFocus);

    setFocusPolicy(Qt::StrongFocus);



    mapHud_ = new CockpitMapHud(frame);

    mapHud_->hide();

    hud_ = new CockpitHudOverlay(frame);

    buildTag_ = new QLabel(QStringLiteral("SDV DIGITAL TWIN"), frame);

    buildTag_->setStyleSheet(QStringLiteral(

        "color: #7dd3fc; font-size: 11px; font-weight: 700; background: rgba(0,0,0,0.55);"

        " padding: 4px 8px; border-radius: 6px;"));

    buildTag_->setAttribute(Qt::WA_TransparentForMouseEvents, true);



    stack->addWidget(carlaView_, 0, 0);

    stack->addWidget(mapboxView_, 0, 0);

    stack->addWidget(mapHud_, 0, 0);

    stack->addWidget(buildTag_, 0, 0, Qt::AlignTop | Qt::AlignHCenter);

    stack->addWidget(hud_, 0, 0);

    hud_->raise();



    outer->addWidget(frame, 1);



    instrumentDock_ = new CockpitInstrumentDock(this);

    outer->addWidget(instrumentDock_, 0);



    connect(hud_, &CockpitHudOverlay::vehicleSelected, this, &CockpitPanel::vehicleSelected);

    if (carlaView_) {
        connect(carlaView_, &sdv::dash::carla::CarlaCockpitView::cameraFramePresented, this, &CockpitPanel::cameraFramePresented);
    }

}



sdv::dash::mapbox::MiniMapView* CockpitPanel::miniMap() const

{

    if (carlaView_) {

        return carlaView_->miniMap();

    }

    return instrumentDock_ ? instrumentDock_->miniMap() : nullptr;

}



void CockpitPanel::setMapboxBridge(sdv::dash::mapbox::MapboxBridge* bridge)

{

    if (carlaView_) {

        carlaView_->setMapboxBridge(bridge);

    }

    if (mapboxView_) {

        mapboxView_->setMapboxBridge(bridge);

    }

    if (auto* mini = miniMap()) {

        mini->setMapboxBridge(bridge);

    }

}



void CockpitPanel::setTelemetryWebSocketUrl(const QUrl& url)

{

    if (carlaView_) {

        carlaView_->setTelemetryWebSocketUrl(url);

    }

}



void CockpitPanel::setVehicleId(const QString& vehicleId)

{

    if (carlaView_) {

        carlaView_->setVehicleId(vehicleId);

    }

}



void CockpitPanel::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)

{

    if (carlaView_) {

        carlaView_->bindPipeline(pipeline);

    }

    if (mapboxView_) {

        mapboxView_->bindPipeline(pipeline);

    }

    if (hud_) {

        hud_->bindPipeline(pipeline);

    }

    if (mapHud_) {

        mapHud_->bindPipeline(pipeline);

    }

    if (instrumentDock_) {

        instrumentDock_->bindPipeline(pipeline);

    }

}



void CockpitPanel::setDriverViewChrome(const bool visible)

{

    if (mapHud_) {

        mapHud_->setVisible(false);

    }

    if (hud_) {

        hud_->setVisible(visible);

    }

}

void CockpitPanel::setPygameMapCockpitMode(const bool enabled)
{
    if (carlaView_) {
        carlaView_->setPygameMapCockpitMode(enabled);
    }
    if (hud_) {
        hud_->setVisible(!enabled);
    }
    if (buildTag_) {
        buildTag_->setVisible(!enabled);
    }
    if (instrumentDock_) {
        instrumentDock_->setPygameMapCockpitMode(enabled);
    }
}

void CockpitPanel::setFullMapVisible(const bool visible)

{

    if (mapboxView_) {

        mapboxView_->setVisible(visible);

        if (visible) {

            mapboxView_->raise();

        }

    }

    if (carlaView_) {

        carlaView_->setVisible(!visible);

    }

    if (hud_) {

        hud_->setVisible(!visible);

    }

    if (mapHud_ && visible) {

        mapHud_->setVisible(true);

        mapHud_->raise();

    }

}



void CockpitPanel::updateFromCarState(const QJsonObject& carState, const bool mqttConnected)

{

    if (carlaView_) {

        carlaView_->updateFromCarState(carState, mqttConnected);

    }

    if (hud_) {

        hud_->updateFromCarState(carState, mqttConnected);

    }

    if (mapHud_) {

        mapHud_->updateFromCarState(carState, mqttConnected);

    }

    if (instrumentDock_) {

        instrumentDock_->updateFromCarState(carState, mqttConnected);

    }

}



}  // namespace sdv::dash::ui

