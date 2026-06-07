#include "mapbox/MapboxCockpitView.h"

#include "mapbox/MapboxBridge.h"
#include "mapbox/MapboxGeoJson.h"
#include "mapbox/MapboxWebConfig.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ops/viewmodels/PerceptionViewModel.h"
#include "ops/viewmodels/TrajectoryViewModel.h"

#include <cmath>

#include <QtMath>

#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSizePolicy>
#include <QVariant>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineView>

namespace sdv::dash::mapbox {

namespace {

constexpr double kDemoCenterX = 100.0;
constexpr double kDemoCenterY = 200.0;
/** Straight segment length (m) for ping-pong demo along local +X (≈ east on Mapbox). */
constexpr double kDemoSegLenM = 160.0;
constexpr double kDemoSpeedMps = 14.0;

void demoPoseAt(const double traveledM, double& x, double& y, double& yawRad)
{
    x = kDemoCenterX + traveledM;
    y = kDemoCenterY;
    yawRad = 0.0;
}

QString arrowKeyFromQt(const int key)
{
    switch (key) {
    case Qt::Key_Up:
        return QStringLiteral("ArrowUp");
    case Qt::Key_Down:
        return QStringLiteral("ArrowDown");
    case Qt::Key_Left:
        return QStringLiteral("ArrowLeft");
    case Qt::Key_Right:
        return QStringLiteral("ArrowRight");
    default:
        return {};
    }
}

}  // namespace

MapboxCockpitView::MapboxCockpitView(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    webView_ = new QWebEngineView(this);
    webView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    webView_->setContextMenuPolicy(Qt::NoContextMenu);
    webView_->setFocusPolicy(Qt::StrongFocus);
    configureMapWebView(webView_);
    layout->addWidget(webView_);
    webView_->installEventFilter(this);

    connect(webView_, &QWebEngineView::loadFinished, this, &MapboxCockpitView::onPageLoaded);

    pushTimer_.setInterval(80);
    pushTimer_.setSingleShot(true);
    connect(&pushTimer_, &QTimer::timeout, this, &MapboxCockpitView::pushViewModelsToBridge);

    demoTimer_.setInterval(200);
    connect(&demoTimer_, &QTimer::timeout, this, &MapboxCockpitView::tickBuiltinDemo);

    cockpitDriveTimer_.setInterval(200);
    connect(&cockpitDriveTimer_, &QTimer::timeout, this, &MapboxCockpitView::driveCockpitFromCpp);

}

void MapboxCockpitView::setMapboxBridge(MapboxBridge* bridge)
{
    bridge_ = bridge;
    setupWebChannel();
    if (webView_) {
        webView_->load(QUrl(QStringLiteral("qrc:/mapbox/index.html")));
    }
}

void MapboxCockpitView::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_ || !bridge_) {
        ensureDemoTimer();
        return;
    }

    const auto schedule = [this]() {
        if (!pushTimer_.isActive()) {
            pushTimer_.start();
        }
        if (hasLiveEgo()) {
            stopDemoTimer();
        }
    };

    connect(pipeline->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, schedule);
    connect(pipeline->trajectoryViewModel(), &sdv::dash::ops::TrajectoryViewModel::changed, this, schedule);
    connect(pipeline->perceptionViewModel(), &sdv::dash::ops::PerceptionViewModel::changed, this, schedule);

    schedule();
    ensureDemoTimer();
}

bool MapboxCockpitView::hasLiveEgo() const
{
    return pipeline_ && pipeline_->egoViewModel()->pose().isValid();
}

void MapboxCockpitView::ensureDemoTimer()
{
    // Route-following demo runs in mapbox-scene.js (Mapbox Directions + SdvRouteSnap).
    demoActive_ = !hasLiveEgo();
}

void MapboxCockpitView::stopDemoTimer()
{
    demoActive_ = false;
    demoTimer_.stop();
}

void MapboxCockpitView::tickBuiltinDemo()
{
    // No-op: JS route demo drives ego when no live telemetry.
}

void MapboxCockpitView::driveCockpitFromCpp()
{
    if (!pageReady_ || !webView_ || cockpitAtRouteEnd_) {
        return;
    }
    cockpitDriveDistM_ += 14.0 * 0.2;
    const QString script = QStringLiteral(
        "(function(d){"
        "  try {"
        "    if (typeof window.__sdvDriveFrame === 'function') {"
        "      return window.__sdvDriveFrame(d);"
        "    }"
        "    return 'no frame';"
        "  } catch (e) { return String(e); }"
        "})('%1')")
                             .arg(cockpitDriveDistM_, 0, 'f', 1);
    webView_->page()->runJavaScript(script, [this](const QVariant& result) {
        if (result.toString() == QLatin1String("end")) {
            cockpitAtRouteEnd_ = true;
        }
    });
}

void MapboxCockpitView::injectManualArrowKey(const QString& arrowKey, const bool pressed)
{
    if (!pageReady_ || !webView_ || arrowKey.isEmpty()) {
        return;
    }
    const QString script = QStringLiteral(
        "(function(k,p){"
        "  try {"
        "    if (typeof window.__sdvManualKey === 'function') {"
        "      window.__sdvManualKey(k, p);"
        "    }"
        "  } catch (e) {}"
        "})('%1', %2)")
                             .arg(arrowKey, pressed ? QStringLiteral("true") : QStringLiteral("false"));
    webView_->page()->runJavaScript(script);
}

void MapboxCockpitView::postManualArrowKey(const int /*qtKey*/, const bool /*pressed*/)
{
    // Manual arrow-key drive disabled.
}

bool MapboxCockpitView::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    Q_UNUSED(event);
    return QWidget::eventFilter(watched, event);
}

void MapboxCockpitView::setupWebChannel()
{
    if (!webView_ || !bridge_) {
        return;
    }

    if (!webChannel_) {
        webChannel_ = new QWebChannel(this);
    }
    webView_->page()->setWebChannel(webChannel_);
    webChannel_->registerObject(QStringLiteral("mapboxBridge"), bridge_);

    if (pageReady_) {
        bridge_->notifyTokenReady();
    }
}

void MapboxCockpitView::onPageLoaded(const bool ok)
{
    if (!ok) {
        return;
    }
    pageReady_ = true;
    setupWebChannel();
    bridge_->notifyTokenReady();
    pushViewModelsToBridge();
    ensureDemoTimer();

    cockpitDriveDistM_ = 0.0;
    cockpitAtRouteEnd_ = false;
    cockpitDriveTimer_.start();
    webView_->setFocus();

    QTimer::singleShot(400, this, [this]() {
        if (!webView_) {
            return;
        }
        webView_->setFocus();
        webView_->page()->runJavaScript(QStringLiteral(
            "(function(){"
            "  try {"
            "    if (typeof window.__sdvKickCockpit === 'function') {"
            "      return window.__sdvKickCockpit();"
            "    }"
            "    return 'no kick';"
            "  } catch (e) { return String(e); }"
            "})()"));
    });
}

QString MapboxCockpitView::syntheticForwardTrajectoryJson(
    const double cx, const double cy, const double yawRad, const double lengthM, const int points)
{
    QJsonArray coords;
    const int n = qMax(8, points);
    const double c = qCos(yawRad);
    const double s = qSin(yawRad);
    for (int i = 0; i <= n; ++i) {
        const double d = (lengthM * i) / n;
        coords.append(QJsonArray{cx + d * c, cy + d * s, 0.5});
    }
    const QJsonObject feature{
        {QStringLiteral("type"), QStringLiteral("Feature")},
        {QStringLiteral("geometry"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("LineString")},
             {QStringLiteral("coordinates"), coords},
         }},
    };
    return QString::fromUtf8(QJsonDocument(feature).toJson(QJsonDocument::Compact));
}

QString MapboxCockpitView::syntheticPerceptionJson(const double cx, const double cy, const double yawRad)
{
    QJsonArray features;
    const struct Obj {
        double dx;
        double dy;
        QString label;
        double r;
    } objs[] = {
        {18.0, 6.0, QStringLiteral("vehicle"), 2.8},
        {-12.0, 14.0, QStringLiteral("vehicle"), 2.4},
        {8.0, -16.0, QStringLiteral("pedestrian"), 1.2},
        {22.0, -8.0, QStringLiteral("cyclist"), 1.6},
    };

    const double c = qCos(yawRad);
    const double s = qSin(yawRad);
    for (const auto& o : objs) {
        const double rx = o.dx * c - o.dy * s;
        const double ry = o.dx * s + o.dy * c;
        features.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("Feature")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("class"), o.label},
                 {QStringLiteral("label"), o.label},
                 {QStringLiteral("radius"), o.r},
             }},
            {QStringLiteral("geometry"),
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("Point")},
                 {QStringLiteral("coordinates"), QJsonArray{cx + rx, cy + ry, 0.5}},
             }},
        });
    }

    const QJsonObject collection{
        {QStringLiteral("type"), QStringLiteral("FeatureCollection")},
        {QStringLiteral("features"), features},
    };
    return QString::fromUtf8(QJsonDocument(collection).toJson(QJsonDocument::Compact));
}

QString MapboxCockpitView::trajectoryGeoJson(
    const sdv::dash::ops::OpsTelemetryPipeline* pipeline, MapboxBridge* mapbox)
{
    if (!pipeline || !mapbox) {
        return {};
    }

    QJsonArray coords;
    const auto& traj = pipeline->trajectoryViewModel()->trajectory();
    if (traj.isValid() && !traj.points.isEmpty()) {
        for (const auto& pt : traj.points) {
            coords.append(QJsonArray{pt.x, pt.y, pt.z});
        }
    } else {
        for (const auto& wp : pipeline->trajectoryViewModel()->waypoints()) {
            coords.append(QJsonArray{wp.x, wp.y, wp.z});
        }
    }
    return geo::lineFeatureJsonFromLocalCoords(coords, mapbox->mapOriginLng(), mapbox->mapOriginLat());
}

QString MapboxCockpitView::perceptionGeoJson(
    const sdv::dash::ops::OpsTelemetryPipeline* pipeline, MapboxBridge* mapbox)
{
    if (!pipeline || !mapbox) {
        return {};
    }

    QJsonArray features;
    for (const auto& obj : pipeline->perceptionViewModel()->objects().objects) {
        const double radius = qMax(obj.lengthM, obj.widthM) * 0.5;
        const double r = radius > 0 ? radius : 2.0;
        const bool isTl = obj.classification.contains(QStringLiteral("traffic_light"), Qt::CaseInsensitive);
        const double tlRadius = isTl ? 7.0 : r;
        QJsonObject props{
            {QStringLiteral("class"), obj.classification},
            {QStringLiteral("id"), obj.objectId},
            {QStringLiteral("label"), obj.classification},
            {QStringLiteral("radius"), tlRadius},
        };
        if (!obj.trafficLightState.isEmpty()) {
            props.insert(QStringLiteral("tl_state"), obj.trafficLightState.trimmed().toLower());
        }
        features.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("Feature")},
            {QStringLiteral("properties"), props},
            {QStringLiteral("geometry"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("Point")},
                {QStringLiteral("coordinates"), QJsonArray{obj.x, obj.y, obj.z}},
            }},
        });
    }
    if (features.isEmpty() || !mapbox) {
        return {};
    }

    QJsonArray out;
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
                 geo::metersToLngLatCoord(coords.at(0).toDouble(), coords.at(1).toDouble(), z, mapbox->mapOriginLng(),
                                          mapbox->mapOriginLat()));
        QJsonObject f = feat;
        f.insert(QStringLiteral("geometry"), g);
        out.append(f);
    }
    if (out.isEmpty()) {
        return {};
    }
    const QJsonObject collection{
        {QStringLiteral("type"), QStringLiteral("FeatureCollection")},
        {QStringLiteral("features"), out},
    };
    return QString::fromUtf8(QJsonDocument(collection).toJson(QJsonDocument::Compact));
}

void MapboxCockpitView::pushViewModelsToBridge()
{
    if (!bridge_ || !pageReady_) {
        return;
    }

    double cx = kDemoCenterX;
    double cy = kDemoCenterY;
    double yaw = 0.0;
    double speed = 12.0;
    bool havePose = false;

    if (pipeline_) {
        const auto& pose = pipeline_->egoViewModel()->pose();
        if (pose.isValid()) {
            havePose = true;
            cx = pose.x;
            cy = pose.y;
            yaw = pose.yawRad;
            speed = pose.speedMps;
        }
    }

    const QString cam = bridge_->cameraMode();
    const bool opsMap = cam == QStringLiteral("operations") || cam == QStringLiteral("dashboard");

    if (opsMap && havePose) {
        bridge_->updateEgoPose(cx, cy, 0.5, yaw, speed, QStringLiteral("map"));
        const QString traj = trajectoryGeoJson(pipeline_, bridge_);
        if (!traj.isEmpty()) {
            bridge_->updateTrajectory(traj);
        }
    }

    QString perc = pipeline_ ? perceptionGeoJson(pipeline_, bridge_) : QString();
    if (perc.isEmpty() && !opsMap && (havePose || demoActive_)) {
        perc = syntheticPerceptionJson(cx, cy, yaw);
    }
    if (!perc.isEmpty()) {
        bridge_->updatePerception(perc);
    }

    if (!havePose && !opsMap) {
        ensureDemoTimer();
    }
}

}  // namespace sdv::dash::mapbox
