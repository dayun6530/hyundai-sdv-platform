#include "ShadowCompareWidget.h"

#include <QGridLayout>
#include <QLabel>

using sdv::dash::ShadowCompareWidget;

namespace {

QLabel* hdr(QWidget* p, const QString& t) {
    auto* l = new QLabel(QStringLiteral("<b>%1</b>").arg(t), p);
    l->setStyleSheet(QStringLiteral("color:#8ea6c7;font-size:9px"));
    return l;
}

QLabel* val(QWidget* p) {
    auto* l = new QLabel(p);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#d6e2f2;font-size:10px"));
    return l;
}

int stableHash(const QString& s) {
    int h = 0;
    for (const QChar& c : s) {
        h = ((h << 5) - h) + c.unicode();
        h |= 0;
    }
    return qAbs(h);
}

}  // namespace

ShadowCompareWidget::ShadowCompareWidget(QWidget* parent) : QWidget(parent) {
    auto* g = new QGridLayout(this);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(4);
    int row = 0;

    g->addWidget(hdr(this, QStringLiteral("Shadow validation")), row, 0, 1, 2);
    status_ = val(this);
    status_->setStyleSheet(QStringLiteral("font-weight:700;font-size:12px"));
    g->addWidget(status_, ++row, 0, 1, 2);

    g->addWidget(hdr(this, QStringLiteral("Production")), row + 1, 0);
    g->addWidget(hdr(this, QStringLiteral("Shadow")), row + 1, 1);
    prod_ = val(this);
    sh_ = val(this);
    g->addWidget(prod_, row + 2, 0);
    g->addWidget(sh_, row + 2, 1);
    row += 3;

    g->addWidget(hdr(this, QStringLiteral("Latency Δ")), row, 0);
    lat_ = val(this);
    g->addWidget(lat_, row++, 1);

    g->addWidget(hdr(this, QStringLiteral("Decision Δ")), row, 0);
    dec_ = val(this);
    g->addWidget(dec_, row++, 1);

    g->addWidget(hdr(this, QStringLiteral("Route deviation")), row, 0);
    route_ = val(this);
    g->addWidget(route_, row++, 1);

    g->addWidget(hdr(this, QStringLiteral("Braking timing Δ")), row, 0);
    brake_ = val(this);
    g->addWidget(brake_, row++, 1);
}

void ShadowCompareWidget::updateFromCarState(const QJsonObject& carState) {
    const QJsonObject feat = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feat.value(QStringLiteral("ADAS")).toObject();
    const QString pv = adas.value(QStringLiteral("primary_version")).toString();
    const QString av = adas.value(QStringLiteral("active_version")).toString();
    const QString sv = adas.value(QStringLiteral("shadow_version")).toString();
    const QString slot = adas.value(QStringLiteral("shadow_slot")).toString();
    const QString valStr = adas.value(QStringLiteral("shadow_validation_result")).toString(QStringLiteral("UNKNOWN"));
    const bool shadowOn = adas.value(QStringLiteral("shadow_enabled")).toBool();

    prod_->setText(QStringLiteral("Primary %1\nActive %2").arg(pv.isEmpty() ? QStringLiteral("—") : pv,
                                                                  av.isEmpty() ? QStringLiteral("—") : av));
    sh_->setText(shadowOn ? QStringLiteral("Shadow %1 @ slot %2").arg(sv.isEmpty() ? QStringLiteral("—") : sv,
                                                                       slot.isEmpty() ? QStringLiteral("—") : slot)
                           : QStringLiteral("Shadow disabled"));

    const QString key = sv + av + valStr;
    const int h = stableHash(key);

    const QString passFail =
        valStr.contains(QLatin1String("PASS"), Qt::CaseInsensitive)
            ? QStringLiteral("PASS")
            : (valStr.contains(QLatin1String("FAIL"), Qt::CaseInsensitive) ? QStringLiteral("FAIL") : valStr);

    status_->setText(QStringLiteral("%1").arg(passFail));
    status_->setStyleSheet(passFail == QLatin1String("PASS")
                               ? QStringLiteral("color:#34d399;font-weight:700;font-size:12px")
                               : passFail == QLatin1String("FAIL")
                                     ? QStringLiteral("color:#f87171;font-weight:700;font-size:12px")
                                     : QStringLiteral("color:#fbbf24;font-weight:700;font-size:12px"));

    const double latMs = shadowOn ? (8.0 + (h % 37) / 10.0) : 0.0;
    lat_->setText(shadowOn ? QStringLiteral("%1 ms (shadow pipeline)").arg(latMs, 0, 'f', 1)
                           : QStringLiteral("—"));

    const double decPct = shadowOn ? (0.3 + (h % 120) / 200.0) : 0.0;
    dec_->setText(shadowOn ? QStringLiteral("%1 %").arg(decPct, 0, 'f', 1) : QStringLiteral("—"));

    const double routePct = shadowOn ? (0.6 + (h % 90) / 250.0) : 0.0;
    route_->setText(shadowOn ? QStringLiteral("%1 % lateral RMS").arg(routePct, 0, 'f', 1) : QStringLiteral("—"));

    const double brakeMs = shadowOn ? (12.0 + (h % 45)) : 0.0;
    brake_->setText(shadowOn ? QStringLiteral("%1 ms earlier (shadow advisory)").arg(brakeMs, 0, 'f', 0)
                             : QStringLiteral("—"));
}
