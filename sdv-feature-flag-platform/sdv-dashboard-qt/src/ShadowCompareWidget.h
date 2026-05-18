#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;

namespace sdv::dash {

class ShadowCompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit ShadowCompareWidget(QWidget* parent = nullptr);
    void updateFromCarState(const QJsonObject& carState);

private:
    QLabel* status_{nullptr};
    QLabel* prod_{nullptr};
    QLabel* sh_{nullptr};
    QLabel* lat_{nullptr};
    QLabel* dec_{nullptr};
    QLabel* route_{nullptr};
    QLabel* brake_{nullptr};
};

}  // namespace sdv::dash
