#include "ui/CockpitInstrumentDock.h"

#include "mapbox/MiniMapView.h"
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"
#include "ui/DashboardTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace sdv::dash::ui {

namespace {

QFrame* sectionFrame(const QString& title, QWidget* parent, QLabel** headerOut = nullptr)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("instrumentSection"));
    frame->setStyleSheet(QStringLiteral(
        "QFrame#instrumentSection {"
        "  background: rgba(4,10,22,0.92);"
        "  border: 1px solid rgba(56,189,248,0.28);"
        "  border-radius: 12px;"
        "}"));
    auto* lay = new QVBoxLayout(frame);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);
    auto* hdr = new QLabel(title, frame);
    hdr->setStyleSheet(QStringLiteral(
        "color: #67e8f9; font-size: 24px; font-weight: 700; letter-spacing: 0.5px;"));
    lay->addWidget(hdr);
    if (headerOut) {
        *headerOut = hdr;
    }
    return frame;
}

QString featureVersionLabel(const QJsonObject& feat)
{
    QString ver = feat.value(QStringLiteral("active_version")).toString();
    if (ver.isEmpty()) {
        ver = feat.value(QStringLiteral("primary_version")).toString();
    }
    if (ver.isEmpty()) {
        return QStringLiteral("—");
    }
    if (!ver.startsWith(QLatin1String("v"), Qt::CaseInsensitive)) {
        ver = QStringLiteral("v%1").arg(ver);
    }
    return ver;
}

QString sectionTitleWithVersion(const QString& name, const QJsonObject& feat)
{
    return QStringLiteral("%1 · %2").arg(name, featureVersionLabel(feat));
}

}  // namespace

CockpitInstrumentDock::CockpitInstrumentDock(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("cockpitInstrumentDock"));
    setStyleSheet(QStringLiteral("background: transparent; border: 0;"));
    setMinimumHeight(210);
    setMaximumHeight(248);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 6, 0, 0);
    row->setSpacing(8);

    auto* adasSec = sectionFrame(QStringLiteral("ADAS · —"), this, &adasHdr_);
    auto* adasLay = qobject_cast<QVBoxLayout*>(adasSec->layout());
    auto* speedRow = new QHBoxLayout();
    speedValue_ = new QLabel(QStringLiteral("72"), adasSec);
    speedValue_->setStyleSheet(DashboardTheme::hudChipStyle() + DashboardTheme::hudSpeedStyle());
    auto* unit = new QLabel(QStringLiteral("km/h"), adasSec);
    unit->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 13px;"));
    speedRow->addWidget(speedValue_);
    speedRow->addWidget(unit, 0, Qt::AlignBottom);
    speedRow->addStretch();
    adasLay->addLayout(speedRow);

    adasState_ = new QLabel(QStringLiteral("Lane assist · ACTIVE"), adasSec);
    adasState_->setStyleSheet(QStringLiteral("color: #4ade80; font-size: 12px; font-weight: 600;"));
    adasLay->addWidget(adasState_);
    adasLay->addStretch();

    row->addWidget(adasSec, 28);

    auto* naviSec = sectionFrame(QStringLiteral("NAVI · —"), this, &naviHdr_);
    auto* naviLay = qobject_cast<QVBoxLayout*>(naviSec->layout());
    auto* naviBody = new QHBoxLayout();
    naviBody->setSpacing(8);

    auto* turnCol = new QVBoxLayout();
    maneuverDist_ = new QLabel(QStringLiteral("120 m"), naviSec);
    maneuverDist_->setStyleSheet(QStringLiteral("color: #f8fafc; font-size: 20px; font-weight: 800;"));
    maneuverHint_ = new QLabel(QStringLiteral("↰ 판교 / 분당"), naviSec);
    maneuverHint_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px;"));
    maneuverHint_->setWordWrap(true);
    routeLine_ = new QLabel(QStringLiteral("Route · v4.0"), naviSec);
    routeLine_->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px;"));
    turnCol->addWidget(maneuverDist_);
    turnCol->addWidget(maneuverHint_);
    turnCol->addWidget(routeLine_);
    turnCol->addStretch();
    naviBody->addLayout(turnCol, 0);

    auto* mapHint = new QLabel(QStringLiteral("Mapbox · top-right cockpit"), naviSec);
    mapHint_ = mapHint;
    mapHint->setAlignment(Qt::AlignCenter);
    mapHint->setStyleSheet(QStringLiteral(
        "color: #64748b; font-size: 10px; font-weight: 600;"
        "border: 1px dashed rgba(56,189,248,0.35); border-radius: 8px; padding: 28px 8px;"));
    naviBody->addWidget(mapHint, 1);
    miniMap_ = nullptr;

    naviLay->addLayout(naviBody, 1);
    row->addWidget(naviSec, 44);

    auto* evSec = sectionFrame(QStringLiteral("EV · —"), this, &evHdr_);
    auto* evLay = qobject_cast<QVBoxLayout*>(evSec->layout());

    evSoc_ = new QLabel(QStringLiteral("82%"), evSec);
    evSoc_->setStyleSheet(QStringLiteral("color: #4ade80; font-size: 22px; font-weight: 800;"));
    evLay->addWidget(evSoc_);
    evRange_ = new QLabel(QStringLiteral("Range 312 km"), evSec);
    evRange_->setStyleSheet(QStringLiteral("color: #cbd5e1; font-size: 13px;"));
    evLay->addWidget(evRange_);

    auto* gearRowWidget = new QWidget(evSec);
    gearRowWidget_ = gearRowWidget;
    auto* gearRow = new QHBoxLayout(gearRowWidget);
    gearRow->setContentsMargins(0, 0, 0, 0);
    gearRow->setSpacing(4);
    const auto gearStyle = QStringLiteral(
        "color: #64748b; font-size: 14px; font-weight: 700; padding: 3px 7px; border-radius: 5px;");
    const auto gearActive = QStringLiteral(
        "color: #f8fafc; font-size: 14px; font-weight: 800; padding: 3px 7px;"
        "background: rgba(37,99,235,0.55); border: 1px solid rgba(96,165,250,0.65); border-radius: 5px;");
    gearP_ = new QLabel(QStringLiteral("P"), gearRowWidget);
    gearR_ = new QLabel(QStringLiteral("R"), gearRowWidget);
    gearN_ = new QLabel(QStringLiteral("N"), gearRowWidget);
    gearD_ = new QLabel(QStringLiteral("D"), gearRowWidget);
    for (auto* g : {gearP_, gearR_, gearN_, gearD_}) {
        g->setStyleSheet(gearStyle);
        gearRow->addWidget(g);
    }
    gearD_->setStyleSheet(gearActive);
    evLay->addWidget(gearRowWidget);

    auto* bottomEv = new QHBoxLayout();
    ready_ = new QLabel(QStringLiteral("READY"), evSec);
    ready_->setStyleSheet(QStringLiteral("color: #4ade80; font-size: 12px; font-weight: 700;"));
    evTemp_ = new QLabel(QStringLiteral("22°C"), evSec);
    evTemp_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px;"));
    odo_ = new QLabel(QStringLiteral("ODO 12,480 km"), evSec);
    odo_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 12px;"));
    bottomEv->addWidget(ready_);
    bottomEv->addStretch();
    bottomEv->addWidget(evTemp_);
    bottomEv->addWidget(odo_);
    evLay->addLayout(bottomEv);
    evLay->addStretch();

    row->addWidget(evSec, 28);
}

void CockpitInstrumentDock::bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) {
        return;
    }
    connect(pipeline_->egoViewModel(), &sdv::dash::ops::EgoViewModel::changed, this, &CockpitInstrumentDock::onEgoChanged);
    onEgoChanged();
}

void CockpitInstrumentDock::onEgoChanged()
{
    if (!pipeline_) {
        return;
    }
    const auto& pose = pipeline_->egoViewModel()->pose();
    if (!pose.isValid()) {
        return;
    }
    speedValue_->setText(QString::number(qRound(qMax(0.0, pose.speedMps * 3.6))));
}

void CockpitInstrumentDock::setPygameMapCockpitMode(const bool enabled)
{
    if (mapHint_) {
        mapHint_->setVisible(!enabled);
    }
    if (gearRowWidget_) {
        gearRowWidget_->setVisible(!enabled);
    }
}

void CockpitInstrumentDock::updateFromCarState(const QJsonObject& carState, const bool /*mqttConnected*/)
{
    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    const QJsonObject ev = carState.value(QStringLiteral("ev")).toObject();

    if (pipeline_) {
        onEgoChanged();
    }

    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
    const QJsonObject nav = feats.value(QStringLiteral("Navigation")).toObject();
    const QJsonObject energy = feats.value(QStringLiteral("Energy")).toObject();

    if (adasHdr_) {
        adasHdr_->setText(sectionTitleWithVersion(QStringLiteral("ADAS"), adas));
    }
    if (naviHdr_) {
        naviHdr_->setText(sectionTitleWithVersion(QStringLiteral("NAVI"), nav));
    }
    if (evHdr_) {
        evHdr_->setText(sectionTitleWithVersion(QStringLiteral("EV"), energy));
    }

    const bool adasOn = adas.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0 ||
                        !adas.value(QStringLiteral("active_version")).toString().isEmpty();
    adasState_->setText(adasOn ? QStringLiteral("Lane assist · ACTIVE") : QStringLiteral("Lane assist · STANDBY"));

    const QString navVer = featureVersionLabel(nav);
    routeLine_->setText(QStringLiteral("Route · %1").arg(navVer));

    const QString routeDist = carState.value(QStringLiteral("route_distance")).toString();
    if (!routeDist.isEmpty() && routeDist != QLatin1String("—")) {
        maneuverDist_->setText(routeDist);
    } else {
        const QJsonObject road = carState.value(QStringLiteral("road")).toObject();
        const QString summary = road.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty()) {
            const QRegularExpression re(QStringLiteral("(\\d+\\s*km|\\d+\\s*m)"));
            const auto kmMatch = re.match(summary);
            if (kmMatch.hasMatch()) {
                maneuverDist_->setText(kmMatch.captured(1));
            }
        }
    }

    QString routeHint = carState.value(QStringLiteral("route_direction")).toString();
    if (routeHint.isEmpty() || routeHint == QLatin1String("차량 정지")) {
        const QJsonObject location = carState.value(QStringLiteral("location")).toObject();
        routeHint = location.value(QStringLiteral("label")).toString();
    }
    if (!routeHint.isEmpty() && routeHint != QLatin1String("—") && routeHint != QLatin1String("차량 정지")) {
        maneuverHint_->setText(routeHint.startsWith(QStringLiteral("↰")) ? routeHint
                                                                         : QStringLiteral("↰ %1").arg(routeHint));
    }

    double soc = -1.0;
    if (ota.contains(QStringLiteral("battery"))) {
        soc = ota.value(QStringLiteral("battery")).toDouble();
    } else if (ev.contains(QStringLiteral("soc"))) {
        soc = ev.value(QStringLiteral("soc")).toDouble();
    }
    if (soc >= 0.0) {
        evSoc_->setText(QStringLiteral("%1%").arg(qRound(soc)));
    }

    double rangeKm = -1.0;
    if (ota.contains(QStringLiteral("range_km"))) {
        rangeKm = ota.value(QStringLiteral("range_km")).toDouble();
    } else if (ev.contains(QStringLiteral("range_km"))) {
        rangeKm = ev.value(QStringLiteral("range_km")).toDouble();
    }
    if (rangeKm < 0.0 && soc >= 0.0) {
        rangeKm = soc * 3.8;
    }
    if (rangeKm >= 0.0) {
        evRange_->setText(QStringLiteral("Range %1 km").arg(qRound(rangeKm)));
    }

    if (ev.contains(QStringLiteral("temp_c"))) {
        evTemp_->setText(QStringLiteral("%1°C").arg(qRound(ev.value(QStringLiteral("temp_c")).toDouble())));
    }

    const QString gear = ota.value(QStringLiteral("gear")).toString();
    const QString gearIdle = QStringLiteral(
        "color: #64748b; font-size: 14px; font-weight: 700; padding: 3px 7px; border-radius: 5px;");
    const QString gearActive = QStringLiteral(
        "color: #f8fafc; font-size: 14px; font-weight: 800; padding: 3px 7px;"
        "background: rgba(37,99,235,0.55); border: 1px solid rgba(96,165,250,0.65); border-radius: 5px;");
    for (auto* g : {gearP_, gearR_, gearN_, gearD_}) {
        g->setStyleSheet(gearIdle);
    }
    QLabel* activeGear = gearD_;
    if (gear.compare(QLatin1String("P"), Qt::CaseInsensitive) == 0) {
        activeGear = gearP_;
    } else if (gear.compare(QLatin1String("R"), Qt::CaseInsensitive) == 0) {
        activeGear = gearR_;
    } else if (gear.compare(QLatin1String("N"), Qt::CaseInsensitive) == 0) {
        activeGear = gearN_;
    }
    activeGear->setStyleSheet(gearActive);

    const bool liveEnergy = ota.value(QStringLiteral("battery_source")).toString() == QLatin1String("EvEnergyFeature.log");
    ready_->setText(liveEnergy ? QStringLiteral("LIVE · LOG") : QStringLiteral("READY"));
}

}  // namespace sdv::dash::ui
