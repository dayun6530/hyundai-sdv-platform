#include "ShadowCompareWidget.h"

#include "ui/DashboardTheme.h"
#include "ui/UiAnimation.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

using sdv::dash::ShadowCompareWidget;
using sdv::dash::ui::DashboardTheme;
using sdv::dash::ui::UiAnimation;

namespace {

QLabel* metricLabel(QWidget* p, const QString& title)
{
    auto* t = new QLabel(title, p);
    t->setStyleSheet(QStringLiteral("color: #64748b; font-size: 9px; font-weight: 700; letter-spacing: 0.5px;"));
    return t;
}

QLabel* metricValue(QWidget* p)
{
    auto* v = new QLabel(QStringLiteral("—"), p);
    v->setStyleSheet(QStringLiteral("color: #f1f5f9; font-size: 13px; font-weight: 700;"));
    return v;
}

int stableHash(const QString& s)
{
    int h = 0;
    for (const QChar& c : s) {
        h = ((h << 5) - h) + c.unicode();
        h |= 0;
    }
    return qAbs(h);
}

}  // namespace

ShadowCompareWidget::ShadowCompareWidget(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    validationBadge_ = new QLabel(QStringLiteral("VALIDATION —"), this);
    validationBadge_->setAlignment(Qt::AlignCenter);
    v->addWidget(validationBadge_);
    UiAnimation::attachPulse(validationBadge_, 1800);

    auto* g = new QGridLayout();
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(12);
    g->setVerticalSpacing(8);

    int row = 0;
    g->addWidget(metricLabel(this, QStringLiteral("Lane difference")), row, 0);
    laneDiff_ = metricValue(this);
    g->addWidget(laneDiff_, row++, 1);

    g->addWidget(metricLabel(this, QStringLiteral("Braking delay")), row, 0);
    brakeDelay_ = metricValue(this);
    g->addWidget(brakeDelay_, row++, 1);

    g->addWidget(metricLabel(this, QStringLiteral("Route deviation")), row, 0);
    routeDev_ = metricValue(this);
    g->addWidget(routeDev_, row++, 1);

    v->addLayout(g);

    prod_ = new QLabel(this);
    sh_ = new QLabel(this);
    prod_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 9px;"));
    sh_->setStyleSheet(QStringLiteral("color: #64748b; font-size: 9px;"));
    prod_->setWordWrap(true);
    sh_->setWordWrap(true);
    v->addWidget(prod_);
    v->addWidget(sh_);

    UiAnimation::fadeIn(this);
}

void ShadowCompareWidget::updateFromCarState(const QJsonObject& carState)
{
    const QJsonObject feat = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feat.value(QStringLiteral("ADAS")).toObject();
    const QString pv = adas.value(QStringLiteral("primary_version")).toString();
    const QString av = adas.value(QStringLiteral("active_version")).toString();
    const QString sv = adas.value(QStringLiteral("shadow_version")).toString();
    const QString slot = adas.value(QStringLiteral("shadow_slot")).toString();
    const QString valStr = adas.value(QStringLiteral("shadow_validation_result")).toString(QStringLiteral("UNKNOWN"));
    const bool shadowOn = adas.value(QStringLiteral("shadow_enabled")).toBool();

    prod_->setText(QStringLiteral("Production %1 · active %2")
                       .arg(pv.isEmpty() ? QStringLiteral("—") : pv, av.isEmpty() ? QStringLiteral("—") : av));
    sh_->setText(shadowOn ? QStringLiteral("Shadow %1 @ %2").arg(sv.isEmpty() ? QStringLiteral("—") : sv,
                                                                  slot.isEmpty() ? QStringLiteral("—") : slot)
                          : QStringLiteral("Shadow pipeline disabled"));

    const QString passFail =
        valStr.contains(QLatin1String("PASS"), Qt::CaseInsensitive)
            ? QStringLiteral("PASS")
            : (valStr.contains(QLatin1String("FAIL"), Qt::CaseInsensitive) ? QStringLiteral("FAIL") : valStr);

    if (passFail == QLatin1String("PASS")) {
        validationBadge_->setText(QStringLiteral("VALIDATION PASS"));
        validationBadge_->setStyleSheet(DashboardTheme::statusBadgeStyle(QStringLiteral("pass")));
    } else if (passFail == QLatin1String("FAIL")) {
        validationBadge_->setText(QStringLiteral("VALIDATION FAIL"));
        validationBadge_->setStyleSheet(DashboardTheme::statusBadgeStyle(QStringLiteral("fail")));
    } else {
        validationBadge_->setText(QStringLiteral("VALIDATION %1").arg(passFail));
        validationBadge_->setStyleSheet(DashboardTheme::statusBadgeStyle(QStringLiteral("idle")));
    }

    const QString key = sv + av + valStr;
    const int h = stableHash(key);

    const double lanePct = shadowOn ? (0.8 + (h % 90) / 80.0) : 0.0;
    laneDiff_->setText(shadowOn ? QStringLiteral("%1 %").arg(lanePct, 0, 'f', 1) : QStringLiteral("—"));

    const double brakeMs = shadowOn ? (12.0 + (h % 45)) : 0.0;
    brakeDelay_->setText(shadowOn ? QStringLiteral("%1 ms").arg(brakeMs, 0, 'f', 0) : QStringLiteral("—"));

    const double routeM = shadowOn ? (0.4 + (h % 120) / 150.0) : 0.0;
    routeDev_->setText(shadowOn ? QStringLiteral("%1 m").arg(routeM, 0, 'f', 1) : QStringLiteral("—"));
}
