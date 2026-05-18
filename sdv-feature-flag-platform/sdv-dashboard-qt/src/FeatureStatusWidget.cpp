#include "FeatureStatusWidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

using sdv::dash::FeatureStatusWidget;

namespace {

// ON/OFF follows status-server merged features.*.flag (from MQTT flags/state + test-ui).
bool isFeatureFlagOn(const QJsonObject& fleetFeat) {
    if (fleetFeat.value(QStringLiteral("kill_switch")).toBool()) {
        return false;
    }
    const QString flag = fleetFeat.value(QStringLiteral("flag")).toString();
    if (!flag.isEmpty()) {
        return flag.compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    }
    return false;
}

QString deriveState(const QJsonObject& fleetFeat) {
    return isFeatureFlagOn(fleetFeat) ? QStringLiteral("ON") : QStringLiteral("OFF");
}

QString stateColor(const QString& s) {
    if (s == QLatin1String("ON")) {
        return QStringLiteral("#34d399");
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
    Q_UNUSED(ota);
    Q_UNUSED(mqttFeat);
    const QString verRaw = fleetFeat.value(QStringLiteral("active_version")).toString(QStringLiteral("—"));
    const QString ver =
        verRaw.startsWith(QLatin1String("v"), Qt::CaseInsensitive) ? verRaw : QStringLiteral("v %1").arg(verRaw);
    const bool on = isFeatureFlagOn(fleetFeat);
    const QString st = deriveState(fleetFeat);
    c.title->setText(QStringLiteral("<b>%1</b>").arg(displayName));
    c.ver->setText(ver);
    c.state->setText(st);
    c.state->setStyleSheet(QStringLiteral("color:%1;font-weight:700;font-size:9px").arg(stateColor(st)));
    c.cpu->setText(QStringLiteral("CPU %1%").arg(estCpu(on, displayName == QLatin1String("ADAS"))));
    c.mem->setText(QStringLiteral("MEM %1 MB").arg(estMem(on)));
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
        auto* hbLab = new QLabel(QStringLiteral("HB"), c.frame);
        hbLab->setStyleSheet(QStringLiteral("color:#64748b;font-size:8px"));
        row->addWidget(hbLab);
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
    QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    const QJsonObject runtime = carState.value(QStringLiteral("runtime")).toObject();
    const bool procUp = runtime.value(QStringLiteral("car_process_running")).toBool();

    if (!procUp) {
        auto forceOff = [](const QJsonObject& in) {
            QJsonObject o = in;
            o.insert(QStringLiteral("flag"), QStringLiteral("OFF"));
            o.insert(QStringLiteral("flag_source"), QStringLiteral("vehicle_offline"));
            return o;
        };
        QJsonObject patched;
        patched.insert(QStringLiteral("ADAS"), forceOff(feats.value(QStringLiteral("ADAS")).toObject()));
        patched.insert(QStringLiteral("Navigation"), forceOff(feats.value(QStringLiteral("Navigation")).toObject()));
        patched.insert(QStringLiteral("Energy"), forceOff(feats.value(QStringLiteral("Energy")).toObject()));
        feats = patched;
    }

    const QJsonObject mqttRoot = mqttFeatures.value(QStringLiteral("features")).toObject();
    const QJsonObject adasM = mqttRoot.value(QStringLiteral("AdasFeature")).toObject();
    const QJsonObject navM = mqttRoot.value(QStringLiteral("NavigationFeature")).toObject();
    const QJsonObject evM = mqttRoot.value(QStringLiteral("EvEnergyFeature")).toObject();

    fillCard(adas_, QStringLiteral("ADAS"), feats.value(QStringLiteral("ADAS")).toObject(), adasM, ota);
    fillCard(nav_, QStringLiteral("Navigation"), feats.value(QStringLiteral("Navigation")).toObject(), navM, ota);
    fillCard(energy_, QStringLiteral("EV Energy"), feats.value(QStringLiteral("Energy")).toObject(), evM, ota);
}
