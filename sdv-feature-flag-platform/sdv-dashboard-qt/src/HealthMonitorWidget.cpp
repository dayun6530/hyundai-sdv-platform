#include "HealthMonitorWidget.h"

#include <QDateTime>
#include <QGridLayout>
#include <QLabel>

using sdv::dash::HealthMonitorWidget;

namespace {

QLabel* mkLabel(QWidget* p, const QString& t) {
    auto* l = new QLabel(t, p);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#d6e2f2;font-size:10px"));
    return l;
}

}  // namespace

HealthMonitorWidget::HealthMonitorWidget(QWidget* parent) : QWidget(parent) {
    auto* g = new QGridLayout(this);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(4);
    int row = 0;
    g->addWidget(mkLabel(this, QStringLiteral("CPU (proc est.)")), row, 0);
    cpu_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(cpu_, row++, 1);

    g->addWidget(mkLabel(this, QStringLiteral("Memory (proc est.)")), row, 0);
    mem_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(mem_, row++, 1);

    g->addWidget(mkLabel(this, QStringLiteral("Event throughput")), row, 0);
    events_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(events_, row++, 1);

    g->addWidget(mkLabel(this, QStringLiteral("Heartbeat")), row, 0);
    heartbeat_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(heartbeat_, row++, 1);

    g->addWidget(mkLabel(this, QStringLiteral("Deploy queue / backlog")), row, 0);
    queue_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(queue_, row++, 1);

    g->addWidget(mkLabel(this, QStringLiteral("HTTP observe latency")), row, 0);
    latency_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(latency_, row++, 1);
}

void HealthMonitorWidget::updateFromCarState(const QJsonObject& carState) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 dt = lastWallMs_ ? (now - lastWallMs_) : 0;
    lastWallMs_ = now;

    const QJsonObject runtime = carState.value(QStringLiteral("runtime")).toObject();
    const bool procUp = runtime.value(QStringLiteral("car_process_running")).toBool();

    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    const QJsonObject feat = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feat.value(QStringLiteral("ADAS")).toObject();
    const bool adasOn = adas.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    const QJsonObject nav = feat.value(QStringLiteral("Navigation")).toObject();
    const bool navOn = nav.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    const QJsonObject en = feat.value(QStringLiteral("Energy")).toObject();
    const bool enOn = en.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;

    const int loadUnits = (adasOn ? 22 : 8) + (navOn ? 12 : 5) + (enOn ? 9 : 4) + (procUp ? 10 : 3);
    cpu_->setText(QStringLiteral("%1 %").arg(qBound(4, loadUnits, 92)));

    const int memMb = 420 + (adasOn ? 110 : 0) + (navOn ? 55 : 0) + (enOn ? 40 : 0);
    mem_->setText(QStringLiteral("%1 MB").arg(memMb));

    const QString net = ota.value(QStringLiteral("network")).toString();
    heartbeat_->setText(QStringLiteral("%1 | vehicle process %2")
                            .arg(carState.value(QStringLiteral("last_seen_at")).toString(),
                                  procUp ? QStringLiteral("RUNNING") : QStringLiteral("UNKNOWN/offline")));

    events_->setText(QStringLiteral("poll Δ %1 ms | uplink %2").arg(dt).arg(net));

    const QString phase = ota.value(QStringLiteral("ota_phase")).toString();
    queue_->setText(phase.isEmpty() ? QStringLiteral("idle") : phase);

    latency_->setText(QStringLiteral("%1 ms (fleet/state poll)").arg(qBound(12LL, dt, 4000LL)));
}
