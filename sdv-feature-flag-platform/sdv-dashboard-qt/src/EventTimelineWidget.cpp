#include "EventTimelineWidget.h"

#include "ui/DashboardTheme.h"
#include "ui/UiAnimation.h"

#include <QDateTime>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonObject>
#include <QAbstractAnimation>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

using sdv::dash::EventTimelineWidget;
using sdv::dash::ui::DashboardTheme;
using sdv::dash::ui::UiAnimation;

namespace {

QString compactTime(const QString& ts)
{
    if (ts.size() >= 19) {
        return ts.mid(11, 8);
    }
    return ts.isEmpty() ? QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")) : ts;
}

QString statusForAction(const QString& action, const QString& reason)
{
    const QString joined = action + QLatin1Char(' ') + reason;
    if (joined.contains(QLatin1String("fail"), Qt::CaseInsensitive) ||
        joined.contains(QLatin1String("error"), Qt::CaseInsensitive)) {
        return QStringLiteral("ERROR");
    }
    if (joined.contains(QLatin1String("rollback"), Qt::CaseInsensitive) ||
        joined.contains(QLatin1String("warn"), Qt::CaseInsensitive)) {
        return QStringLiteral("WARNING");
    }
    if (joined.contains(QLatin1String("shadow"), Qt::CaseInsensitive)) {
        return QStringLiteral("SHADOW");
    }
    if (joined.contains(QLatin1String("info"), Qt::CaseInsensitive)) {
        return QStringLiteral("INFO");
    }
    return QStringLiteral("SUCCESS");
}

QString eventForRecord(const QJsonObject& o)
{
    const QString action = o.value(QStringLiteral("action")).toString();
    const QString reason = o.value(QStringLiteral("reason")).toString();
    if (!reason.isEmpty()) {
        return reason;
    }
    return action.isEmpty() ? QStringLiteral("fleet event") : action;
}

QString targetForRecord(const QJsonObject& o)
{
    const QString car = o.value(QStringLiteral("car_id")).toString();
    const QString feat = o.value(QStringLiteral("feature")).toString();
    if (!car.isEmpty() && !feat.isEmpty()) {
        return QStringLiteral("%1 · %2").arg(car, feat);
    }
    return car.isEmpty() ? (feat.isEmpty() ? QStringLiteral("fleet") : feat) : car;
}

QColor dotColor(const QString& status)
{
    if (status == QLatin1String("SUCCESS")) {
        return QColor(QStringLiteral("#4ade80"));
    }
    if (status == QLatin1String("WARNING") || status == QLatin1String("WARN")) {
        return QColor(QStringLiteral("#facc15"));
    }
    if (status == QLatin1String("ERROR")) {
        return QColor(QStringLiteral("#f87171"));
    }
    if (status == QLatin1String("SHADOW")) {
        return QColor(QStringLiteral("#38bdf8"));
    }
    return QColor(QStringLiteral("#64748b"));
}

}  // namespace

EventTimelineWidget::EventTimelineWidget(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setStyleSheet(QStringLiteral(
        "QScrollArea { background: rgba(4,10,20,0.9); border: 1px solid rgba(34,211,238,0.18); border-radius: 12px; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(56,189,248,0.35); border-radius: 3px; }"));

    streamHost_ = new QWidget();
    streamHost_->setStyleSheet(QStringLiteral("background: transparent;"));
    streamLayout_ = new QVBoxLayout(streamHost_);
    streamLayout_->setContentsMargins(8, 8, 8, 8);
    streamLayout_->setSpacing(4);
    streamLayout_->addStretch();

    scroll_->setWidget(streamHost_);
    lay->addWidget(scroll_);
    UiAnimation::fadeIn(scroll_);
}

QWidget* EventTimelineWidget::makeStreamRow(const QString& time, const QString& event, const QString& target, const QString& status)
{
    auto* row = new QFrame(streamHost_);
    row->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(8,16,30,0.85); border: 1px solid rgba(30,41,59,0.9); border-radius: 8px; }"
        "QFrame:hover { border-color: rgba(56,189,248,0.35); background: rgba(12,22,40,0.92); }"));

    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(8);

    auto* dot = new QLabel(QStringLiteral("●"), row);
    dot->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; background: transparent;")
                           .arg(dotColor(status).name()));
    dot->setFixedWidth(14);

    auto* timeLbl = new QLabel(time, row);
    timeLbl->setFixedWidth(58);
    timeLbl->setStyleSheet(QStringLiteral("color: #64748b; font-size: 9px; font-weight: 700; font-family: Consolas;"));

    auto* body = new QVBoxLayout();
    body->setSpacing(2);
    auto* eventLbl = new QLabel(event, row);
    eventLbl->setStyleSheet(QStringLiteral("color: #f1f5f9; font-size: 10px; font-weight: 600;"));
    eventLbl->setWordWrap(true);
    auto* targetLbl = new QLabel(target, row);
    targetLbl->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 9px;"));
    body->addWidget(eventLbl);
    body->addWidget(targetLbl);

    auto* pill = new QLabel(status, row);
    pill->setStyleSheet(DashboardTheme::eventStatusPillStyle(status));
    pill->setAlignment(Qt::AlignCenter);
    pill->setFixedHeight(20);

    h->addWidget(dot);
    h->addWidget(timeLbl);
    h->addLayout(body, 1);
    h->addWidget(pill);

    auto* effect = new QGraphicsOpacityEffect(row);
    effect->setOpacity(0.0);
    row->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", row);
    anim->setDuration(280);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    return row;
}

void EventTimelineWidget::appendLine(const QString& line)
{
    appendRow(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), line, QStringLiteral("runtime"), QStringLiteral("INFO"));
}

void EventTimelineWidget::setFleetHistory(const QJsonArray& records)
{
    while (streamLayout_->count() > 1) {
        QLayoutItem* item = streamLayout_->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    const int n = records.size();
    const int start = qMax(0, n - 40);
    for (int i = start; i < n; ++i) {
        const auto o = records.at(i).toObject();
        const QString action = o.value(QStringLiteral("action")).toString();
        const QString reason = o.value(QStringLiteral("reason")).toString();
        appendRow(compactTime(o.value(QStringLiteral("timestamp")).toString()),
                  eventForRecord(o),
                  targetForRecord(o),
                  statusForAction(action, reason));
    }
}

void EventTimelineWidget::appendRow(const QString& time, const QString& event, const QString& target, const QString& status)
{
    const int maxRows = 60;
    while (streamLayout_->count() > maxRows) {
        QLayoutItem* item = streamLayout_->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    QWidget* row = makeStreamRow(time, event, target, status);
    streamLayout_->insertWidget(streamLayout_->count() - 1, row);
    scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
}
