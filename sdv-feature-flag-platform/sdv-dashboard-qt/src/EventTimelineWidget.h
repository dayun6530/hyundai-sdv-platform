#pragma once

#include <QJsonArray>
#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace sdv::dash {

class EventTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit EventTimelineWidget(QWidget* parent = nullptr);
    void appendLine(const QString& line);
    void setFleetHistory(const QJsonArray& records);

private:
    void appendRow(const QString& time, const QString& event, const QString& target, const QString& status);
    QWidget* makeStreamRow(const QString& time, const QString& event, const QString& target, const QString& status);

    QScrollArea* scroll_{nullptr};
    QVBoxLayout* streamLayout_{nullptr};
    QWidget* streamHost_{nullptr};
};

}  // namespace sdv::dash
