#include "EventTimelineWidget.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QHeaderView>
#include <QJsonObject>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

using sdv::dash::EventTimelineWidget;

namespace {

QString compactTime(const QString& ts) {
    if (ts.size() >= 19) {
        return ts.mid(11, 8);
    }
    return ts.isEmpty() ? QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")) : ts;
}

QString statusForAction(const QString& action, const QString& reason) {
    const QString joined = action + QLatin1Char(' ') + reason;
    if (joined.contains(QLatin1String("fail"), Qt::CaseInsensitive) ||
        joined.contains(QLatin1String("rollback"), Qt::CaseInsensitive)) {
        return QStringLiteral("WARN");
    }
    if (joined.contains(QLatin1String("shadow"), Qt::CaseInsensitive)) {
        return QStringLiteral("SHADOW");
    }
    return QStringLiteral("SUCCESS");
}

QString eventForRecord(const QJsonObject& o) {
    const QString action = o.value(QStringLiteral("action")).toString();
    const QString reason = o.value(QStringLiteral("reason")).toString();
    if (!reason.isEmpty()) {
        return reason;
    }
    return action.isEmpty() ? QStringLiteral("fleet event") : action;
}

QString targetForRecord(const QJsonObject& o) {
    const QString car = o.value(QStringLiteral("car_id")).toString();
    const QString feat = o.value(QStringLiteral("feature")).toString();
    if (!car.isEmpty() && !feat.isEmpty()) {
        return QStringLiteral("%1 / %2").arg(car, feat);
    }
    return car.isEmpty() ? (feat.isEmpty() ? QStringLiteral("fleet") : feat) : car;
}

}  // namespace

EventTimelineWidget::EventTimelineWidget(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({QStringLiteral("TIME"), QStringLiteral("EVENT"), QStringLiteral("TARGET"), QStringLiteral("STATUS")});
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setFont(QFont(QStringLiteral("Consolas"), 8));
    table_->setStyleSheet(QStringLiteral(
        "QTableWidget { background-color:#030711; alternate-background-color:#050d18; color:#b8c7dc;"
        "border:1px solid #17263a; border-radius:6px; gridline-color:#122033; }"
        "QHeaderView::section { background-color:#07111f; color:#6f87a8; border:0; border-bottom:1px solid #1d2d45;"
        "padding:3px 6px; font-size:8px; font-weight:700; }"
        "QTableWidget::item { padding:2px 6px; border-bottom:1px solid #07111f; }"));
    lay->addWidget(table_);
}

void EventTimelineWidget::appendLine(const QString& line) {
    appendRow(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")), line, QStringLiteral("runtime"), QStringLiteral("INFO"));
}

void EventTimelineWidget::setFleetHistory(const QJsonArray& records) {
    table_->setRowCount(0);
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

void EventTimelineWidget::appendRow(const QString& time, const QString& event, const QString& target, const QString& status) {
    if (table_->rowCount() > 70) {
        table_->removeRow(0);
    }
    const int row = table_->rowCount();
    table_->insertRow(row);

    const auto makeItem = [](const QString& text, const QColor& color) {
        auto* item = new QTableWidgetItem(text);
        item->setForeground(color);
        return item;
    };

    const QColor statusColor = status == QLatin1String("SUCCESS")
                                   ? QColor(QStringLiteral("#34d399"))
                                   : status == QLatin1String("WARN")
                                         ? QColor(QStringLiteral("#f97316"))
                                         : status == QLatin1String("SHADOW")
                                               ? QColor(QStringLiteral("#38bdf8"))
                                               : QColor(QStringLiteral("#94a3b8"));
    table_->setItem(row, 0, makeItem(time, QColor(QStringLiteral("#7f93ad"))));
    table_->setItem(row, 1, makeItem(event, QColor(QStringLiteral("#d6e2f2"))));
    table_->setItem(row, 2, makeItem(target, QColor(QStringLiteral("#9fb1ca"))));
    table_->setItem(row, 3, makeItem(status, statusColor));
    table_->scrollToBottom();
}
