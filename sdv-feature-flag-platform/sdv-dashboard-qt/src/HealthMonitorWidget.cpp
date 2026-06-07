#include "HealthMonitorWidget.h"

#include "ui/DashboardTheme.h"
#include "ui/UiAnimation.h"

#include <QDateTime>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QJsonArray>

using sdv::dash::HealthMonitorWidget;
using sdv::dash::ui::DashboardTheme;
using sdv::dash::ui::UiAnimation;

namespace {

QLabel* mkLabel(QWidget* p, const QString& t)
{
    auto* l = new QLabel(t, p);
    l->setStyleSheet(QStringLiteral("color:#94a3b8;font-size:9px;font-weight:700;letter-spacing:0.4px;background:transparent;"));
    return l;
}

QProgressBar* mkBar(QWidget* p, const QString& color)
{
    auto* bar = new QProgressBar(p);
    bar->setRange(0, 100);
    bar->setTextVisible(false);
    bar->setFixedHeight(10);
    bar->setStyleSheet(DashboardTheme::progressBarStyle(color));
    return bar;
}

}  // namespace

HealthMonitorWidget::HealthMonitorWidget(QWidget* parent) : QWidget(parent)
{
    auto* g = new QGridLayout(this);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(5);
    int row = 0;

    auto addMetric = [&](const QString& title, QLabel*& valueLbl, QProgressBar*& bar, const QString& color) {
        g->addWidget(mkLabel(this, title), row, 0);
        bar = mkBar(this, color);
        valueLbl = mkLabel(this, QStringLiteral("—"));
        valueLbl->setStyleSheet(QStringLiteral("color:#e2e8f0;font-size:10px;font-weight:700;background:transparent;"));
        g->addWidget(bar, row, 1);
        g->addWidget(valueLbl, row++, 2);
    };

    addMetric(QStringLiteral("CPU"), cpu_, cpuBar_, QStringLiteral("#22d3ee"));
    addMetric(QStringLiteral("Memory"), mem_, memBar_, QStringLiteral("#38bdf8"));
    addMetric(QStringLiteral("FPS"), fps_, fpsBar_, QStringLiteral("#4ade80"));
    addMetric(QStringLiteral("Latency"), latency_, latencyBar_, QStringLiteral("#facc15"));
    addMetric(QStringLiteral("Topic rate"), topicRate_, topicBar_, QStringLiteral("#a78bfa"));
    addMetric(QStringLiteral("Processes"), processCount_, processBar_, QStringLiteral("#fb7185"));

    g->addWidget(mkLabel(this, QStringLiteral("Event throughput")), row, 0);
    events_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(events_, row++, 1, 1, 2);

    g->addWidget(mkLabel(this, QStringLiteral("Heartbeat")), row, 0);
    heartbeat_ = mkLabel(this, QStringLiteral("—"));
    g->addWidget(heartbeat_, row++, 1, 1, 2);

    g->addWidget(mkLabel(this, QStringLiteral("ROS telemetry")), row, 0);
    rosTelemetry_ = mkLabel(this, QStringLiteral("ROS WS: offline"));
    g->addWidget(rosTelemetry_, row++, 1, 1, 2);

    fpsWindowStart_ = QDateTime::currentMSecsSinceEpoch();
    tickWindowStart_ = fpsWindowStart_;

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &HealthMonitorWidget::refreshRates);
    timer->start(500);

    UiAnimation::fadeIn(this);
    UiAnimation::attachPulse(heartbeat_, 1200);
}

void HealthMonitorWidget::noteCameraFrame()
{
    ++frameCount_;
    refreshRates();
}

void HealthMonitorWidget::noteFleetTick()
{
    ++tickCount_;
    lastFleetMs_ = QDateTime::currentMSecsSinceEpoch();
    refreshRates();
}

void HealthMonitorWidget::refreshRates()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 fpsDt = now - fpsWindowStart_;
    if (fpsDt >= 800) {
        displayFps_ = qBound(0, static_cast<int>(frameCount_ * 1000.0 / fpsDt), 60);
        frameCount_ = 0;
        fpsWindowStart_ = now;
        if (fps_) {
            UiAnimation::animateLabelNumber(fps_, QStringLiteral("%1 fps").arg(displayFps_));
        }
        if (fpsBar_) {
            UiAnimation::animateProgress(fpsBar_, qBound(0, displayFps_ * 100 / 60, 100));
        }
    }

    const qint64 tickDt = now - tickWindowStart_;
    if (tickDt >= 800) {
        displayTopicHz_ = qBound(0, static_cast<int>(tickCount_ * 1000.0 / tickDt), 120);
        tickCount_ = 0;
        tickWindowStart_ = now;
        if (topicRate_) {
            UiAnimation::animateLabelNumber(topicRate_, QStringLiteral("%1 Hz").arg(displayTopicHz_));
        }
        if (topicBar_) {
            UiAnimation::animateProgress(topicBar_, qBound(0, displayTopicHz_, 100));
        }
    }

    if (lastFleetMs_ > 0) {
        displayLatencyMs_ = qBound(0, static_cast<int>(now - lastFleetMs_), 999);
        if (latency_) {
            latency_->setText(QStringLiteral("%1 ms").arg(displayLatencyMs_));
        }
        if (latencyBar_) {
            UiAnimation::animateProgress(latencyBar_, qBound(0, displayLatencyMs_ / 3, 100));
        }
    }
}

void HealthMonitorWidget::setRosTelemetryLine(const QString& line)
{
    if (rosTelemetry_) {
        rosTelemetry_->setText(line.isEmpty() ? QStringLiteral("ROS WS: offline") : line);
        const bool ok = line.contains(QStringLiteral("connected"), Qt::CaseInsensitive);
        rosTelemetry_->setStyleSheet(ok ? QStringLiteral("color:#4ade80;font-size:10px;")
                                        : QStringLiteral("color:#f97316;font-size:10px;"));
    }
}

void HealthMonitorWidget::updateFromFleetStates(const QJsonArray& fleetStates,
                                                const int fleetOnline,
                                                const int fleetTotal)
{
    noteFleetTick();

    int online = fleetOnline;
    int total = fleetTotal > 0 ? fleetTotal : 10;
    if (online <= 0 && !fleetStates.isEmpty()) {
        online = 0;
        for (const auto& v : fleetStates) {
            const QJsonObject o = v.toObject();
            const QJsonObject runtime = o.value(QStringLiteral("runtime")).toObject();
            if (runtime.value(QStringLiteral("car_process_running")).toBool()) {
                ++online;
            } else if (o.value(QStringLiteral("status")).toString().compare(QLatin1String("online"),
                                                                              Qt::CaseInsensitive) == 0) {
                ++online;
            }
        }
        total = fleetStates.size();
    }

    UiAnimation::animateProgress(processBar_, qBound(10, (online * 100) / qMax(1, total), 100));
    processCount_->setText(QStringLiteral("%1/%2 fleet").arg(online).arg(total));

    heartbeat_->setText(online >= total ? QStringLiteral("FLEET OK") : QStringLiteral("PARTIAL"));
    heartbeat_->setStyleSheet(online >= total
                                  ? QStringLiteral("color:#4ade80;font-size:12px;font-weight:800;")
                                  : QStringLiteral("color:#facc15;font-size:12px;font-weight:800;"));
}

void HealthMonitorWidget::updateFromCarState(const QJsonObject& carState)
{
    noteFleetTick();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 dt = lastWallMs_ ? (now - lastWallMs_) : 0;
    lastWallMs_ = now;

    const QJsonObject runtime = carState.value(QStringLiteral("runtime")).toObject();
    const bool procUp = runtime.value(QStringLiteral("car_process_running")).toBool();

    const QJsonObject feat = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feat.value(QStringLiteral("ADAS")).toObject();
    const bool adasOn = adas.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    const QJsonObject nav = feat.value(QStringLiteral("Navigation")).toObject();
    const bool navOn = nav.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    const QJsonObject en = feat.value(QStringLiteral("Energy")).toObject();
    const bool enOn = en.value(QStringLiteral("flag")).toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;

    int processes = 1;
    if (adasOn) {
        ++processes;
    }
    if (navOn) {
        ++processes;
    }
    if (enOn) {
        ++processes;
    }
    if (procUp) {
        ++processes;
    }

    const int loadUnits = (adasOn ? 22 : 8) + (navOn ? 12 : 5) + (enOn ? 9 : 4) + (procUp ? 10 : 3);
    const int cpuPct = qBound(4, loadUnits, 92);
    UiAnimation::animateProgress(cpuBar_, cpuPct);
    cpu_->setText(QStringLiteral("%1 %").arg(cpuPct));

    const int memMb = 420 + (adasOn ? 180 : 0) + (navOn ? 90 : 0) + (enOn ? 70 : 0);
    const int memPct = qBound(8, memMb / 12, 94);
    UiAnimation::animateProgress(memBar_, memPct);
    mem_->setText(QStringLiteral("%1 MB").arg(memMb));

    UiAnimation::animateProgress(processBar_, qBound(10, processes * 18, 100));
    processCount_->setText(QString::number(processes));

    const int eps = dt > 0 ? qBound(40, static_cast<int>(8000 / qMax<qint64>(dt, 1)), 220) : 152;
    UiAnimation::animateLabelNumber(events_, QStringLiteral("%1 msg/s").arg(eps));

    heartbeat_->setText(procUp ? QStringLiteral("OK") : QStringLiteral("DEGRADED"));
    heartbeat_->setStyleSheet(procUp ? QStringLiteral("color:#4ade80;font-size:12px;font-weight:800;")
                                     : QStringLiteral("color:#facc15;font-size:12px;font-weight:800;"));
}
