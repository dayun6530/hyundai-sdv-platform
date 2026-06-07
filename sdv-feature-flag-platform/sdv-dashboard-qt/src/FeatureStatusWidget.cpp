#include "FeatureStatusWidget.h"

#include "ui/DashboardTheme.h"
#include "ui/UiAnimation.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>

using sdv::dash::FeatureStatusWidget;
using sdv::dash::ui::DashboardTheme;
using sdv::dash::ui::UiAnimation;

namespace {

bool isFeatureFlagOn(const QJsonObject& fleetFeat)
{
    if (fleetFeat.value(QStringLiteral("kill_switch")).toBool()) {
        return false;
    }
    const QString flag = fleetFeat.value(QStringLiteral("flag")).toString();
    if (!flag.isEmpty()) {
        return flag.compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    }
    return false;
}

int estCpu(const bool on, const bool heavy)
{
    if (!on) {
        return 6;
    }
    return heavy ? 38 : 22;
}

int estMem(const bool on)
{
    return on ? 168 : 96;
}

}  // namespace

void FeatureStatusWidget::styleCard(QFrame* f, const QString& accentRgb)
{
    f->setObjectName(QStringLiteral("featureCard"));
    f->setStyleSheet(DashboardTheme::featureCardStyle(accentRgb));
    f->setCursor(Qt::PointingHandCursor);
}

void FeatureStatusWidget::fillCard(CardUi& c, const QString& displayName, const QJsonObject& fleetFeat,
                                   const QJsonObject& /*mqttFeat*/, const QJsonObject& /*ota*/)
{
    const QString verRaw = fleetFeat.value(QStringLiteral("active_version")).toString(QStringLiteral("—"));
    const QString ver =
        verRaw.startsWith(QLatin1String("v"), Qt::CaseInsensitive) ? verRaw : QStringLiteral("v%1").arg(verRaw);
    const bool on = isFeatureFlagOn(fleetFeat);

    c.title->setText(displayName);
    c.ver->setText(ver);
    c.badge->setText(on ? QStringLiteral("RUNNING") : QStringLiteral("STANDBY"));
    c.badge->setStyleSheet(on ? DashboardTheme::statusBadgeStyle(QStringLiteral("running"))
                              : DashboardTheme::statusBadgeStyle(QStringLiteral("idle")));

    const int cpu = estCpu(on, displayName == QLatin1String("ADAS"));
    const int mem = estMem(on);
    UiAnimation::animateProgress(c.cpuBar, cpu);
    UiAnimation::animateProgress(c.memBar, qBound(8, mem / 3, 94));
    c.cpuLbl->setText(QStringLiteral("CPU %1%").arg(cpu));
    c.memLbl->setText(QStringLiteral("MEM %1 MB").arg(mem));

    c.health->setText(hbOn_ && on ? QStringLiteral("● Health OK") : QStringLiteral("○ Health"));
    c.health->setStyleSheet(on && hbOn_ ? QStringLiteral("color: #4ade80; font-size: 9px; font-weight: 600;")
                                        : QStringLiteral("color: #64748b; font-size: 9px;"));
}

FeatureStatusWidget::FeatureStatusWidget(QWidget* parent) : QWidget(parent)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    auto mkCard = [&](CardUi& c, const QString& accent) {
        c.frame = new QFrame(this);
        styleCard(c.frame, accent);
        auto* v = new QVBoxLayout(c.frame);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(6);

        auto* head = new QHBoxLayout();
        c.title = new QLabel(c.frame);
        c.title->setStyleSheet(QStringLiteral("color: #f1f5f9; font-size: 11px; font-weight: 800;"));
        c.badge = new QLabel(QStringLiteral("—"), c.frame);
        head->addWidget(c.title, 1);
        head->addWidget(c.badge, 0, Qt::AlignRight);
        v->addLayout(head);

        c.ver = new QLabel(c.frame);
        c.ver->setStyleSheet(QStringLiteral("color: #7dd3fc; font-size: 10px; font-weight: 600;"));
        v->addWidget(c.ver);

        c.cpuBar = new QProgressBar(c.frame);
        c.cpuBar->setRange(0, 100);
        c.cpuBar->setTextVisible(false);
        c.cpuBar->setFixedHeight(8);
        c.cpuBar->setStyleSheet(DashboardTheme::progressBarStyle(QStringLiteral("#22d3ee")));
        c.cpuLbl = new QLabel(c.frame);
        c.cpuLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 9px;"));
        v->addWidget(c.cpuLbl);
        v->addWidget(c.cpuBar);

        c.memBar = new QProgressBar(c.frame);
        c.memBar->setRange(0, 100);
        c.memBar->setTextVisible(false);
        c.memBar->setFixedHeight(8);
        c.memBar->setStyleSheet(DashboardTheme::progressBarStyle(QStringLiteral("#38bdf8")));
        c.memLbl = new QLabel(c.frame);
        c.memLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 9px;"));
        v->addWidget(c.memLbl);
        v->addWidget(c.memBar);

        c.health = new QLabel(c.frame);
        v->addWidget(c.health);

        h->addWidget(c.frame, 1);
        UiAnimation::attachPulse(c.badge, 1600);
    };

    mkCard(adas_, QStringLiteral("34, 211, 238"));
    mkCard(nav_, QStringLiteral("52, 211, 153"));
    mkCard(energy_, QStringLiteral("251, 191, 36"));

    connect(&hbTimer_, &QTimer::timeout, this, &FeatureStatusWidget::pulseHeartbeat);
    hbTimer_.start(520);
    UiAnimation::fadeIn(this);
}

void FeatureStatusWidget::pulseHeartbeat()
{
    hbOn_ = !hbOn_;
    if (!lastCar_.isEmpty()) {
        updateCards(lastCar_, lastMqtt_);
    }
}

void FeatureStatusWidget::updateCards(const QJsonObject& carState, const QJsonObject& mqttFeatures)
{
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
    fillCard(adas_, QStringLiteral("ADAS"), feats.value(QStringLiteral("ADAS")).toObject(),
             mqttRoot.value(QStringLiteral("AdasFeature")).toObject(), ota);
    fillCard(nav_, QStringLiteral("Navigation"), feats.value(QStringLiteral("Navigation")).toObject(),
             mqttRoot.value(QStringLiteral("NavigationFeature")).toObject(), ota);
    fillCard(energy_, QStringLiteral("EV Energy"), feats.value(QStringLiteral("Energy")).toObject(),
             mqttRoot.value(QStringLiteral("EvEnergyFeature")).toObject(), ota);
}
