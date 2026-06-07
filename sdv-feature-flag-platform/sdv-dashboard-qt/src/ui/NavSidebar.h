#pragma once

#include <QFrame>
#include <QString>

class QButtonGroup;
class QPushButton;

namespace sdv::dash::ui {

class NavSidebar : public QFrame {
    Q_OBJECT
public:
    enum class Section {
        Home = 0,
        Vehicle,
        Adas,
        Navigation,
        Ev,
        Deployment,
        AbTest,
        Shadow,
        Settings,
    };
    Q_ENUM(Section)

    explicit NavSidebar(QWidget* parent = nullptr);

    Section currentSection() const;

signals:
    void sectionSelected(Section section);

private:
    QButtonGroup* group_{nullptr};
    Section current_{Section::Home};
};

}  // namespace sdv::dash::ui
