#include "FeatureStatusWidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

using sdv::dash::FeatureStatusWidget;

namespace {

QString deriveState(const QJsonObject& fleetFeat, const QJsonObject& mqttFeat, const QJsonObject& ota) {
    const bool kill = fleetFeat.value(QStringLiteral("kill_switch")).toBool();
    const bool shadow = fleetFeat.value(QStringLiteral("shadow_enabled")).toBool();
    const QString pend = fleetFeat.value(QStringLiteral("pending_active_version")).toString();
    const QString flag = fleetFeat.value(QStringLiteral("flag")).toString();
    const QString upd = ota.value(QStringLiteral("update_mode")).toString();
    const QString phase = ota.value(QStringLiteral("ota_phase")).toString();

    if (kill) {
        return QStringLiteral("FAILED");
    }
    if (upd.compare(QLatin1String("FAILED"), Qt::CaseInsensitive) == 0 ||
        phase.contains(QLatin1String("FAIL"), Qt::CaseInsensitive)) {
        return QStringLiteral("FAILED");
    }
    const bool blocked = fleetFeat.contains(QStringLiteral("blocked_versions")) &&
                         fleetFeat.value(QStringLiteral("blocked_versions")).toArray().size() > 0;
    if (blocked && phase.contains(QLatin1String("ROLL"), Qt::CaseInsensitive)) {
        return QStringLiteral("ROLLBACK");
    }
    if (!pend.isEmpty()) {
        return QStringLiteral("DEPLOYING");
    }
    if (shadow) {
        return QStringLiteral("SHADOW");
    }
    if (flag.compare(QLatin1String("OFF"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("DISABLED");
    }
    const bool mqttMain = mqttFeat.value(QStringLiteral("main")).toBool(true);
    if (!mqttFeat.isEmpty() && !mqttMain) {
        return QStringLiteral("DISABLED");
    }
    return QStringLiteral("RUNNING");
}

QString stateColor(const QString& s) {
    if (s == QLatin1String("RUNNING")) {
        return QStringLiteral("#34d399");
    }
    if (s == QLatin1String("SHADOW")) {
        return QStringLiteral("#38bdf8");
    }
    if (s == QLatin1String("DEPLOYING")) {
        return QStringLiteral("#fbbf24");
    }
    if (s == QLatin1String("FAILED")) {
        return QStringLiteral("#f87171");
    }
    if (s == QLatin1String("ROLLBACK")) {
        return QStringLiteral("#fb923c");
    }
    return QStringLiteral("#94a3b8");
}

int estCpu(bool on, bool heavy) {
    if (!on) {
        return 6;
    }
    return heavy ? 38 : 22;
}

int estMem(bool on) {
    return on ? 168 : 96;
}

}  // namespace

void FeatureStatusWidget::styleCard(QFrame* f, const QString& accent) {
    f->setFrameShape(QFrame::StyledPanel);
    f->setStyleSheet(QStringLiteral(
        "QFrame { background-color:#07111d;border:1px solid %1;border-radius:7px;padding:3px; }"
        "QLabel { color:#d6e2f2; font-size:9px; }")
                         .arg(accent));
}

void FeatureStatusWidget::fillCard(CardUi& c, const QString& displayName, const QJsonObject& fleetFeat,
                                   const QJsonObject& mqttFeat, const QJsonObject& ota) {
    const QString ver = fleetFeat.value(QStringLiteral("active_version")).toString(QStringLiteral("—"));
    const QString st = deriveState(fleetFeat, mqttFeat, ota);
    c.title->setText(QStringLiteral("<b>%1</b>").arg(displayName));
    c.ver->setText(QStringLiteral("v %1").arg(ver));
    c.state->setText(st);
    c.state->setStyleSheet(QStringLiteral("color:%1;font-weight:700;font-size:9px").arg(stateColor(st)));

    const bool logicalOn =
        fleetFeat.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0 &&
        !fleetFeat.value(QStringLiteral("kill_switch")).toBool();
    const bool mqttMain = mqttFeat.value(QStringLiteral("main")).toBool(true);
    const bool on = logicalOn && mqttMain;
    c.cpu->setText(QStringLiteral("%1 %").arg(estCpu(on, displayName == QLatin1String("ADAS"))));
    c.mem->setText(QStringLiteral("%1 MB").arg(estMem(on)));
    c.hb->setText(hbOn_ ? QStringLiteral("●") : QStringLiteral("○"));
    c.hb->setStyleSheet(on ? QStringLiteral("color:#22c55e") : QStringLiteral("color:#475569"));
}

FeatureStatusWidget::FeatureStatusWidget(QWidget* parent) : QWidget(parent) {
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(5);

    auto mkCard = [&](CardUi& c, const QString& accent) {
        c.frame = new QFrame(this);
        styleCard(c.frame, accent);
        auto* v = new QVBoxLayout(c.frame);
        c.title = new QLabel(c.frame);
        c.ver = new QLabel(c.frame);
        c.state = new QLabel(c.frame);
        c.hb = new QLabel(c.frame);
        c.cpu = new QLabel(c.frame);
        c.mem = new QLabel(c.frame);
        v->setContentsMargins(5, 5, 5, 5);
        v->setSpacing(3);
        v->addWidget(c.title);
        v->addWidget(c.ver);
        v->addWidget(c.state);
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(2);
        row->addWidget(new QLabel(QStringLiteral("HB"), c.frame));
        row->addStretch();
        row->addWidget(c.hb);
        v->addLayout(row);
        v->addWidget(c.cpu);
        v->addWidget(c.mem);
        h->addWidget(c.frame, 1);
    };

    mkCard(adas_, QStringLiteral("#1e3a5f"));
    mkCard(nav_, QStringLiteral("#1a3f3a"));
    mkCard(energy_, QStringLiteral("#3f2a1a"));

    connect(&hbTimer_, &QTimer::timeout, this, &FeatureStatusWidget::pulseHeartbeat);
    hbTimer_.start(520);
}

void FeatureStatusWidget::pulseHeartbeat() {
    hbOn_ = !hbOn_;
    if (!lastCar_.isEmpty()) {
        updateCards(lastCar_, lastMqtt_);
    }
}

void FeatureStatusWidget::updateCards(const QJsonObject& carState, const QJsonObject& mqttFeatures) {
    lastCar_ = carState;
    lastMqtt_ = mqttFeatures;
    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();

    const QJsonObject mqttRoot = mqttFeatures.value(QStringLiteral("features")).toObject();
    const QJsonObject adasM = mqttRoot.value(QStringLiteral("AdasFeature")).toObject();
    const QJsonObject navM = mqttRoot.value(QStringLiteral("NavigationFeature")).toObject();
    const QJsonObject evM = mqttRoot.value(QStringLiteral("EvEnergyFeature")).toObject();

    fillCard(adas_, QStringLiteral("ADAS"), feats.value(QStringLiteral("ADAS")).toObject(), adasM, ota);
    fillCard(nav_, QStringLiteral("Navigation"), feats.value(QStringLiteral("Navigation")).toObject(), navM, ota);
    fillCard(energy_, QStringLiteral("EV Energy"), feats.value(QStringLiteral("Energy")).toObject(), evM, ota);
}
