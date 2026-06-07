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
    QLabel* validationBadge_{nullptr};
    QLabel* laneDiff_{nullptr};
    QLabel* brakeDelay_{nullptr};
    QLabel* routeDev_{nullptr};
    QLabel* prod_{nullptr};
    QLabel* sh_{nullptr};
};

}  // namespace sdv::dash
