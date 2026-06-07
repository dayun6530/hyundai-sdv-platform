#include "ABTestingWidget.h"

#include "ui/DashboardTheme.h"
#include "ui/UiAnimation.h"

#include <cmath>

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QVBoxLayout>

using sdv::dash::ABTestingWidget;
using sdv::dash::TrafficSplitBar;
using sdv::dash::ui::UiAnimation;

namespace sdv::dash {

class TrafficSplitBar : public QWidget {
public:
    explicit TrafficSplitBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(22);
        setMaximumHeight(22);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setSplit(int pctA)
    {
        pctA_ = qBound(0, pctA, 100);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect r = rect().adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 23, 42, 220));
        p.drawRoundedRect(r, 10, 10);

        const int wA = r.width() * pctA_ / 100;
        if (wA > 0) {
            QLinearGradient g(r.left(), 0, r.left() + wA, 0);
            g.setColorAt(0.0, QColor(14, 116, 144));
            g.setColorAt(1.0, QColor(34, 211, 238));
            p.setBrush(g);
            p.drawRoundedRect(QRect(r.left(), r.top(), wA, r.height()), 10, 10);
        }
        if (wA < r.width()) {
            p.setBrush(QColor(30, 41, 59, 240));
            p.drawRoundedRect(QRect(r.left() + wA, r.top(), r.width() - wA, r.height()), 10, 10);
        }
        p.setPen(QColor(56, 189, 248, 80));
        p.drawRoundedRect(r, 10, 10);
    }

private:
    int pctA_{50};
};

}  // namespace sdv::dash

ABTestingWidget::ABTestingWidget(QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    strategy_ = new QLabel(this);
    strategy_->setStyleSheet(QStringLiteral("color: #e2e8f0; font-size: 10px; font-weight: 600;"));
    canary_ = new QLabel(this);
    canary_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 9px;"));
    v->addWidget(strategy_);
    v->addWidget(canary_);

    auto* splitHdr = new QLabel(QStringLiteral("Traffic Split"), this);
    splitHdr->setStyleSheet(QStringLiteral("color: #64748b; font-size: 9px; font-weight: 700; letter-spacing: 0.6px;"));
    v->addWidget(splitHdr);

    splitBar_ = new TrafficSplitBar(this);
    v->addWidget(splitBar_);

    auto* verRow = new QHBoxLayout();
    versionA_ = new QLabel(QStringLiteral("Version A"), this);
    versionB_ = new QLabel(QStringLiteral("Version B"), this);
    versionA_->setStyleSheet(QStringLiteral("color: #67e8f9; font-size: 9px; font-weight: 700;"));
    versionB_->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 9px; font-weight: 700;"));
    verRow->addWidget(versionA_);
    verRow->addStretch();
    verRow->addWidget(versionB_);
    v->addLayout(verRow);

    auto* pctRow = new QHBoxLayout();
    pctA_ = new QLabel(QStringLiteral("A — 0%"), this);
    pctB_ = new QLabel(QStringLiteral("B — 0%"), this);
    pctA_->setStyleSheet(QStringLiteral("color: #22d3ee; font-size: 11px; font-weight: 800;"));
    pctB_->setStyleSheet(QStringLiteral("color: #64748b; font-size: 11px; font-weight: 700;"));
    pctRow->addWidget(pctA_);
    pctRow->addStretch();
    pctRow->addWidget(pctB_);
    v->addLayout(pctRow);

    barA_ = new QProgressBar(this);
    barB_ = new QProgressBar(this);
    barA_->hide();
    barB_->hide();

    UiAnimation::fadeIn(this);
}

void ABTestingWidget::updateRollup(const QJsonArray& fleetStates)
{
    int a = 0;
    int b = 0;
    QString strategy = QStringLiteral("uniform");
    QString groupHint;
    QString verA;
    QString verB;

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
        const QString ver = adas.value(QStringLiteral("active_version")).toString();
        if (slot == QLatin1String("B")) {
            ++b;
            if (verB.isEmpty() && !ver.isEmpty()) {
                verB = ver;
            }
        } else {
            ++a;
            if (verA.isEmpty() && !ver.isEmpty()) {
                verA = ver;
            }
        }
    }
    const int total = a + b;
    pctATotal_ = total ? qRound(100.0 * a / total) : 0;
    const int pctBTotal = total ? (100 - pctATotal_) : 0;

    if (!groupHint.isEmpty()) {
        strategy = QStringLiteral("Constraint rollout · %1").arg(groupHint);
    }

    strategy_->setText(strategy);
    canary_->setText(QStringLiteral("%1 vehicles · ADAS slot cohort").arg(total));
    versionA_->setText(verA.isEmpty() ? QStringLiteral("Version A") : QStringLiteral("A · %1").arg(verA));
    versionB_->setText(verB.isEmpty() ? QStringLiteral("Version B") : QStringLiteral("B · %1").arg(verB));
    pctA_->setText(QStringLiteral("A %1%").arg(pctATotal_));
    pctB_->setText(QStringLiteral("B %1%").arg(pctBTotal));
    splitBar_->setSplit(pctATotal_);
    barA_->setValue(pctATotal_);
    barB_->setValue(pctBTotal);
}

void ABTestingWidget::tickTrafficShift()
{
    anim_ += 0.04f;
    const float wobble = 1.5f * std::sin(anim_);
    const int adj = qBound(0, pctATotal_ + qRound(wobble), 100);
    splitBar_->setSplit(adj);
}
