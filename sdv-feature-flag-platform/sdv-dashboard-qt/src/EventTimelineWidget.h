#pragma once

#include <QJsonArray>
#include <QWidget>

class QTableWidget;

namespace sdv::dash {

class EventTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit EventTimelineWidget(QWidget* parent = nullptr);
    void appendLine(const QString& line);
    void setFleetHistory(const QJsonArray& records);

private:
    void appendRow(const QString& time, const QString& event, const QString& target, const QString& status);

    QTableWidget* table_{nullptr};
};

}  // namespace sdv::dash
