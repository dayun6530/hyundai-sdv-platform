#include "carla/CarlaCockpitView.h"



#include "carla/CarlaCameraReceiver.h"

#include "carla/CinematicOverlay.h"

#include "carla/HudOverlayWidget.h"

#include "carla/LaneOverlayRenderer.h"

#include "carla/NavigationOverlayRenderer.h"

#include "carla/PerceptionOverlayRenderer.h"

#include "mapbox/MapboxBridge.h"

#include "mapbox/MapboxGeoJson.h"

#include "mapbox/MiniMapView.h"

#include "ops/OpsTelemetryPipeline.h"

#include "ui/DashboardTheme.h"

#include "ui/UiAnimation.h"

#include "ops/viewmodels/EgoViewModel.h"

#include "ops/viewmodels/PerceptionViewModel.h"

#include "ops/viewmodels/TrajectoryViewModel.h"



#include <QGridLayout>

#include <QJsonArray>

#include <QJsonDocument>

#include <QJsonObject>

#include <QLabel>

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <QTimer>

#include <QVBoxLayout>



namespace sdv::dash::carla {



CarlaCockpitView::CarlaCockpitView(QWidget* parent) : QWidget(parent)

{

    setMinimumHeight(420);

    setStyleSheet(QStringLiteral("background: #02060f;"));



    auto* stack = new QGridLayout(this);

    stack->setContentsMargins(0, 0, 0, 0);

    stack->setSpacing(0);



    videoLabel_ = new QLabel(this);

    videoLabel_->setAlignment(Qt::AlignCenter);

    videoLabel_->setScaledContents(false);

    videoLabel_->setStyleSheet(QStringLiteral("background: #0b1220;"));

    videoLabel_->setMinimumSize(320, 240);



    laneOverlay_ = new LaneOverlayRenderer(this);

    navigationOverlay_ = new NavigationOverlayRenderer(this);

    perceptionOverlay_ = new PerceptionOverlayRenderer(this);

    cinematicOverlay_ = new CinematicOverlay(this);

    hudOverlay_ = new HudOverlayWidget(this);



    miniMapChrome_ = new QFrame(this);

    miniMapChrome_->setObjectName(QStringLiteral("miniMapChrome"));

    miniMapChrome_->setStyleSheet(sdv::dash::ui::DashboardTheme::miniMapChromeStyle());

    auto* miniLay = new QVBoxLayout(miniMapChrome_);

    miniLay->setContentsMargins(4, 4, 4, 4);

    miniMap_ = new sdv::dash::mapbox::MiniMapView(miniMapChrome_);

    miniMap_->setFixedSize(248, 164);

    miniMap_->setStyleSheet(QStringLiteral("border: 0; border-radius: 10px; background: transparent;"));

    miniLay->addWidget(miniMap_);

    miniMapChrome_->setFixedSize(miniMap_->width() + 8, miniMap_->height() + 8);



    stack->addWidget(videoLabel_, 0, 0);

    stack->addWidget(laneOverlay_, 0, 0);

    stack->addWidget(navigationOverlay_, 0, 0);

    stack->addWidget(perceptionOverlay_, 0, 0);

    stack->addWidget(cinematicOverlay_, 0, 0);

    stack->addWidget(hudOverlay_, 0, 0);

    stack->addWidget(miniMapChrome_, 0, 0, Qt::AlignTop | Qt::AlignRight);



    camera_ = new CarlaCameraReceiver(this);

    connect(camera_, &CarlaCameraReceiver::frameReady, this, &CarlaCockpitView::onCameraFrame);

    cameraUiTimer_ = new QTimer(this);
    cameraUiTimer_->setSingleShot(true);
    cameraUiTimer_->setInterval(50);
    connect(cameraUiTimer_, &QTimer::timeout, this, &CarlaCockpitView::flushPendingCameraFrame);

    auto* pushTimer = new QTimer(this);

    pushTimer->setInterval(120);

    connect(pushTimer, &QTimer::timeout, this, &CarlaCockpitView::pushMiniMapTelemetry);

    pushTimer->start();

}



CarlaCockpitView::~CarlaCockpitView() = default;



void CarlaCockpitView::setMapboxBridge(sdv::dash::mapbox::MapboxBridge* bridge)

{

    mapboxBridge_ = bridge;

    if (miniMap_) {

        miniMap_->setMapboxBridge(bridge);

    }

    if (mapboxBridge_) {

        connect(

            mapboxBridge_,

            &sdv::dash::mapbox::MapboxBridge::navGuidanceChanged,

            this,

            &CarlaCockpitView::onNavGuidance,

            Qt::UniqueConnection);

    }

}



void CarlaCockpitView::setTelemetryWebSocketUrl(const QUrl& url)

{

    if (url.isValid()) {

        wsUrl_ = url;

    }

    if (camera_ && !pipelineCameraOnly_) {

        camera_->start(wsUrl_);

    } else if (camera_ && pipelineCameraOnly_) {

        camera_->stop();

    }

}



void CarlaCockpitView::setVehicleId(const QString& vehicleId)

{

    if (hudOverlay_) {

        hudOverlay_->setVehicleId(vehicleId);

    }

}



void CarlaCockpitView::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)

{

    pipeline_ = pipeline;

    if (pipeline_ && camera_) {
        connect(
            pipeline_, &sdv::dash::ops::OpsTelemetryPipeline::frontCameraJsonReceived, this,
            [this](const QByteArray& json) {
                if (camera_) {
                    camera_->ingestJsonEnvelope(json);
                }
            },
            Qt::QueuedConnection);
    }

    if (laneOverlay_) {

        laneOverlay_->bindPipeline(pipeline);

    }

    if (navigationOverlay_) {

        navigationOverlay_->bindPipeline(pipeline);

    }

    if (perceptionOverlay_) {

        perceptionOverlay_->bindPipeline(pipeline);

    }

    if (hudOverlay_) {

        hudOverlay_->bindPipeline(pipeline);

    }

    pushMiniMapTelemetry();

}



void CarlaCockpitView::updateFromCarState(const QJsonObject& carState, const bool mqttConnected)

{

    if (hudOverlay_) {

        hudOverlay_->updateFromCarState(carState, mqttConnected);

    }

}



void CarlaCockpitView::ingestCameraFrame(const QImage& frame)

{

    if (camera_) {

        camera_->ingestFrame(frame);

    }

}

void CarlaCockpitView::setPipelineCameraOnly(const bool enabled)
{
    pipelineCameraOnly_ = enabled;
    if (!camera_) {
        return;
    }
    if (enabled) {
        camera_->stop();
        camera_->showWaitingPlaceholder();
    } else if (wsUrl_.isValid()) {
        camera_->start(wsUrl_);
    }
}

void CarlaCockpitView::setPygameMapCockpitMode(const bool enabled)
{
    pygameMapCockpit_ = enabled;
    setPipelineCameraOnly(enabled);
    const bool showOverlays = !enabled;
    for (QWidget* layer :
         {static_cast<QWidget*>(laneOverlay_), static_cast<QWidget*>(navigationOverlay_),
          static_cast<QWidget*>(perceptionOverlay_), static_cast<QWidget*>(cinematicOverlay_),
          static_cast<QWidget*>(hudOverlay_)}) {
        if (layer) {
            layer->setVisible(showOverlays);
        }
    }
    if (videoLabel_) {
        videoLabel_->setStyleSheet(
            enabled ? QStringLiteral("background: #02060f;")
                    : QStringLiteral("background: #0b1220;"));
    }
    if (miniMapChrome_) {
        miniMapChrome_->setVisible(!enabled);
    }
    layoutOverlays();
}



void CarlaCockpitView::resizeEvent(QResizeEvent* event)

{

    QWidget::resizeEvent(event);

    layoutOverlays();

}

void CarlaCockpitView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    QWidget::mousePressEvent(event);
}

void CarlaCockpitView::layoutOverlays()

{

    const QRect r = rect();

    if (videoLabel_) {

        videoLabel_->setGeometry(r);

    }

    for (QWidget* layer : {static_cast<QWidget*>(laneOverlay_),

                           static_cast<QWidget*>(navigationOverlay_),

                           static_cast<QWidget*>(perceptionOverlay_),

                           static_cast<QWidget*>(cinematicOverlay_),

                           static_cast<QWidget*>(hudOverlay_)}) {

        if (layer) {

            layer->setGeometry(r);

        }

    }

    if (miniMapChrome_) {

        const int margin = 12;

        miniMapChrome_->move(width() - miniMapChrome_->width() - margin, margin);

        if (miniMapChrome_->isVisible()) {
            miniMapChrome_->raise();
        }

    }

}



void CarlaCockpitView::onCameraFrame(const QImage& frame)

{

    if (frame.isNull()) {
        return;
    }
    pendingCameraFrame_ = frame;
    if (cameraUiTimer_ && !cameraUiTimer_->isActive()) {
        cameraUiTimer_->start();
    }

}

void CarlaCockpitView::flushPendingCameraFrame()
{
    const QImage frame = pendingCameraFrame_;
    if (frame.isNull() || !videoLabel_) {
        return;
    }

    lastFrame_ = frame;

    const QSize target = videoLabel_->size();
    if (target.isEmpty()) {
        return;
    }

    QPixmap pixmap;
    const QImage scaled = frame.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (scaled.width() - target.width()) / 2;
    const int y = (scaled.height() - target.height()) / 2;
    pixmap = QPixmap::fromImage(scaled.copy(x, y, target.width(), target.height()));
    videoLabel_->setPixmap(pixmap);

    if (!fadedIn_ && !pygameMapCockpit_) {
        fadedIn_ = true;
        sdv::dash::ui::UiAnimation::fadeIn(this, 500);
    } else if (pygameMapCockpit_) {
        fadedIn_ = true;
    }

    emit cameraFramePresented();
}



void CarlaCockpitView::onNavGuidance(const QString& arrows, const QString& distanceText)

{

    if (hudOverlay_) {

        hudOverlay_->updateNavGuidance(arrows, distanceText);

    }

    if (navigationOverlay_) {

        navigationOverlay_->setNavGuidance(arrows, distanceText);

    }

}



void CarlaCockpitView::pushMiniMapTelemetry()

{

    if (!miniMap_ || !pipeline_ || !mapboxBridge_) {

        return;

    }

    const auto& pose = pipeline_->egoViewModel()->pose();

    if (pose.isValid()) {

        mapboxBridge_->updateEgoPose(pose.x, pose.y, pose.z, pose.yawRad, pose.speedMps, pose.frameId);

    }



    const auto& traj = pipeline_->trajectoryViewModel()->trajectory();

    if (traj.isValid() && !traj.points.isEmpty()) {

        QJsonArray coords;

        for (const auto& pt : traj.points) {

            coords.append(QJsonArray{pt.x, pt.y, pt.z});

        }

        const QString geo = sdv::dash::mapbox::geo::lineFeatureJsonFromLocalCoords(
            coords, mapboxBridge_->mapOriginLng(), mapboxBridge_->mapOriginLat());

        if (!geo.isEmpty()) {

            mapboxBridge_->updateTrajectory(geo);

        }

    }



    const auto& objects = pipeline_->perceptionViewModel()->objects();

    if (objects.isValid()) {

        QJsonArray features;

        for (const auto& obj : objects.objects) {

            QJsonObject props{

                {QStringLiteral("class"), obj.classification},

                {QStringLiteral("label"), obj.classification},

            };

            if (!obj.trafficLightState.isEmpty()) {

                props.insert(QStringLiteral("tl_state"), obj.trafficLightState.trimmed().toLower());

            }

            const bool isTl =

                obj.classification.contains(QStringLiteral("traffic_light"), Qt::CaseInsensitive);

            if (isTl) {

                props.insert(QStringLiteral("radius"), 7.0);

            }

            features.append(QJsonObject{

                {QStringLiteral("type"), QStringLiteral("Feature")},

                {QStringLiteral("properties"), props},

                {QStringLiteral("geometry"),

                 QJsonObject{

                     {QStringLiteral("type"), QStringLiteral("Point")},

                     {QStringLiteral("coordinates"), QJsonArray{obj.x, obj.y, obj.z}},

                 }},

            });

        }

        QJsonArray lngLatFeatures;

        for (const QJsonValue& v : features) {

            const QJsonObject feat = v.toObject();

            const QJsonObject geom = feat.value(QStringLiteral("geometry")).toObject();

            const QJsonArray coords = geom.value(QStringLiteral("coordinates")).toArray();

            if (coords.size() < 2) {

                continue;

            }

            const double z = coords.size() > 2 ? coords.at(2).toDouble() : 0.5;

            QJsonObject g = geom;

            g.insert(QStringLiteral("coordinates"),

                     sdv::dash::mapbox::geo::metersToLngLatCoord(coords.at(0).toDouble(), coords.at(1).toDouble(), z,

                                                                   mapboxBridge_->mapOriginLng(),

                                                                   mapboxBridge_->mapOriginLat()));

            QJsonObject f = feat;

            f.insert(QStringLiteral("geometry"), g);

            lngLatFeatures.append(f);

        }

        if (!lngLatFeatures.isEmpty()) {

            const QJsonObject fc{

                {QStringLiteral("type"), QStringLiteral("FeatureCollection")},

                {QStringLiteral("features"), lngLatFeatures},

            };

            mapboxBridge_->updatePerception(QString::fromUtf8(QJsonDocument(fc).toJson(QJsonDocument::Compact)));

        }

    }

}



}  // namespace sdv::dash::carla

