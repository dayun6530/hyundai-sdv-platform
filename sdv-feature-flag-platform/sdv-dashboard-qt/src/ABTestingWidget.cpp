#include "ABTestingWidget.h"

#include <cmath>

#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QVBoxLayout>

using sdv::dash::ABTestingWidget;

ABTestingWidget::ABTestingWidget(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);
    strategy_ = new QLabel(this);
    strategy_->setWordWrap(true);
    canary_ = new QLabel(this);
    canary_->setWordWrap(true);
    strategy_->setStyleSheet(QStringLiteral("color:#d6e2f2;font-size:10px"));
    canary_->setStyleSheet(QStringLiteral("color:#9fb1ca;font-size:10px"));
    v->addWidget(strategy_);
    v->addWidget(canary_);

    auto* g = new QGridLayout();
    g->setContentsMargins(0, 0, 0, 0);
    g->setVerticalSpacing(4);
    pctA_ = new QLabel(QStringLiteral("A: —%"), this);
    pctB_ = new QLabel(QStringLiteral("B: —%"), this);
    barA_ = new QProgressBar(this);
    barB_ = new QProgressBar(this);
    barA_->setRange(0, 100);
    barB_->setRange(0, 100);
    barA_->setTextVisible(true);
    barB_->setTextVisible(true);
    barA_->setFormat(QStringLiteral("Slot A"));
    barB_->setFormat(QStringLiteral("Slot B"));
    g->addWidget(pctA_, 0, 0);
    g->addWidget(barA_, 1, 0);
    g->addWidget(pctB_, 2, 0);
    g->addWidget(barB_, 3, 0);
    v->addLayout(g);

    sliderDemo_ = new QSlider(Qt::Horizontal, this);
    sliderDemo_->setRange(0, 100);
    sliderDemo_->setValue(78);
    sliderDemo_->setEnabled(false);
    auto* hint = new QLabel(QStringLiteral("Fleet-driven split (read-only)"), this);
    hint->setStyleSheet(QStringLiteral("color:#64748b;font-size:9px"));
    v->addWidget(hint);
    v->addWidget(sliderDemo_);
}

void ABTestingWidget::updateRollup(const QJsonArray& fleetStates) {
    int a = 0;
    int b = 0;
    QString strategy = QStringLiteral("uniform");
    QString groupHint;

    const int n = fleetStates.size();
    for (int i = 0; i < n; ++i) {
        const auto car = fleetStates.at(i).toObject();
        const QString grp = car.value(QStringLiteral("group")).toString();
        if (!grp.isEmpty() && grp.compare(QLatin1String("uniform"), Qt::CaseInsensitive) != 0) {
            groupHint = grp;
        }
        const QJsonObject feats = car.value(QStringLiteral("features")).toObject();
        const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
        const QString slot = adas.value(QStringLiteral("active_slot")).toString().trimmed().toUpper();
        if (slot == QLatin1String("B")) {
            ++b;
        } else {
            ++a;
        }
    }
    const int total = a + b;
    const int pctATotal = total ? qRound(100.0 * a / total) : 0;
    const int pctBTotal = total ? (100 - pctATotal) : 0;

    if (!groupHint.isEmpty()) {
        strategy = QStringLiteral("constraint rollout (%1)").arg(groupHint);
    }

    strategy_->setText(QStringLiteral("<b>Strategy</b><br>%1").arg(strategy));
    canary_->setText(QStringLiteral("<b>Canary cohort</b><br>%1 vehicles sampled — ADAS slot traffic")
                         .arg(total));

    pctA_->setText(QStringLiteral("A traffic: %1%").arg(pctATotal));
    pctB_->setText(QStringLiteral("B traffic: %1%").arg(pctBTotal));
    barA_->setValue(pctATotal);
    barB_->setValue(pctBTotal);
    sliderDemo_->setValue(pctATotal);
}

void ABTestingWidget::tickTrafficShift() {
    anim_ += 0.04f;
    const float wobble = 2.f * std::sin(anim_);
    const int base = barA_->value();
    const int adj = qBound(0, base + qRound(wobble), 100);
    barA_->setFormat(QStringLiteral("Slot A  (~%1%)").arg(adj));
}
