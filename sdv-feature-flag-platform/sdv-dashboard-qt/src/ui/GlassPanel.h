#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QVBoxLayout;

namespace sdv::dash::ui {

class GlassPanel : public QFrame {
    Q_OBJECT
public:
    explicit GlassPanel(const QString& title, QWidget* parent = nullptr);

    void setContentWidget(QWidget* widget);
    QWidget* contentWidget() const { return content_; }

private:
    QLabel* title_{nullptr};
    QWidget* content_{nullptr};
    QVBoxLayout* bodyLayout_{nullptr};
};

}  // namespace sdv::dash::ui
