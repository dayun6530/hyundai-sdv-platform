#include "ui/NavSidebar.h"

#include "ui/DashboardTheme.h"

#include <QGraphicsDropShadowEffect>
#include <QButtonGroup>
#include <QPushButton>
#include <QVBoxLayout>

namespace sdv::dash::ui {

NavSidebar::NavSidebar(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("navSidebar"));
    setFixedWidth(88);
    setStyleSheet(DashboardTheme::sidebarStyle());

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 140, 255, 35));
    setGraphicsEffect(shadow);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 14, 8, 14);
    v->setSpacing(4);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);

    auto add = [v, this](const QString& icon, const QString& label, const Section section) {
        auto* btn = new QPushButton(QStringLiteral("%1\n%2").arg(icon, label), this);
        btn->setObjectName(QStringLiteral("navBtn"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        group_->addButton(btn);
        v->addWidget(btn);
        connect(btn, &QPushButton::toggled, this, [this, section](const bool on) {
            if (!on) {
                return;
            }
            current_ = section;
            emit sectionSelected(section);
        });
        return btn;
    };

    add(QStringLiteral("⌂"), QStringLiteral("Home"), Section::Home);
    add(QStringLiteral("⬡"), QStringLiteral("Vehicle"), Section::Vehicle);
    add(QStringLiteral("◎"), QStringLiteral("ADAS"), Section::Adas);
    add(QStringLiteral("➤"), QStringLiteral("Nav"), Section::Navigation);
    add(QStringLiteral("⚡"), QStringLiteral("EV"), Section::Ev);
    add(QStringLiteral("⬇"), QStringLiteral("Deploy"), Section::Deployment);
    add(QStringLiteral("⇄"), QStringLiteral("A/B"), Section::AbTest);
    add(QStringLiteral("◑"), QStringLiteral("Shadow"), Section::Shadow);
    v->addStretch();
    add(QStringLiteral("⚙"), QStringLiteral("Settings"), Section::Settings);

    if (auto* first = group_->button(0)) {
        first->setChecked(true);
    }
}

NavSidebar::Section NavSidebar::currentSection() const
{
    return current_;
}

}  // namespace sdv::dash::ui
