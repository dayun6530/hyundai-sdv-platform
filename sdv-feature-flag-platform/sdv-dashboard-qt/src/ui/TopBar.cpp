#include "ui/TopBar.h"



#include "ui/DashboardTheme.h"



#include <QDateTime>

#include <QGraphicsDropShadowEffect>

#include <QHBoxLayout>

#include <QLabel>

#include <QPushButton>

#include <QTimer>

#include <QVBoxLayout>



namespace sdv::dash::ui {



TopBar::TopBar(QWidget* parent) : QFrame(parent)

{

    setObjectName(QStringLiteral("topBar"));

    setFixedHeight(52);

    setStyleSheet(DashboardTheme::topBarStyle());



    auto* shadow = new QGraphicsDropShadowEffect(this);

    shadow->setBlurRadius(20);

    shadow->setOffset(0, 3);

    shadow->setColor(QColor(0, 120, 255, 30));

    setGraphicsEffect(shadow);



    auto* h = new QHBoxLayout(this);

    h->setContentsMargins(16, 8, 16, 8);

    h->setSpacing(12);



    auto* brandCol = new QVBoxLayout();

    brandCol->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("Hyundai SDV — Feature Operations Center"), this);

    title->setObjectName(QStringLiteral("brandTitle"));

    auto* sub = new QLabel(QStringLiteral("Fleet telemetry · OTA · Shadow validation"), this);

    sub->setObjectName(QStringLiteral("brandSub"));

    brandCol->addWidget(title);

    brandCol->addWidget(sub);

    h->addLayout(brandCol, 1);



    auto* modes = new QHBoxLayout();

    modes->setSpacing(4);

    modeButtons_.append(makeSegment(QStringLiteral("1st Person"), true));

    modeButtons_.append(makeSegment(QStringLiteral("3rd Person"), false));

    modeButtons_.append(makeSegment(QStringLiteral("Dashboard"), false));

    modeButtons_.append(makeSegment(QStringLiteral("Operations"), false));

    for (auto* btn : modeButtons_) {

        modes->addWidget(btn);

    }

    h->addLayout(modes);



    clock_ = new QLabel(this);

    clock_->setObjectName(QStringLiteral("clockLabel"));

    h->addWidget(clock_);



    auto* timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, &TopBar::tickClock);

    timer->start(1000);

    tickClock();

}



QPushButton* TopBar::makeSegment(const QString& label, const bool active)

{

    auto* btn = new QPushButton(label, this);

    btn->setCheckable(true);

    btn->setChecked(active);

    btn->setCursor(Qt::PointingHandCursor);

    btn->setStyleSheet(DashboardTheme::segmentButtonStyle(active));

    connect(btn, &QPushButton::clicked, this, [this, btn]() { selectMode(btn); });

    return btn;

}



void TopBar::selectMode(QPushButton* active)

{

    for (auto* btn : modeButtons_) {

        const bool on = btn == active;

        btn->setChecked(on);

        btn->setStyleSheet(DashboardTheme::segmentButtonStyle(on));

    }

    emit viewModeChanged(active->text());

}



void TopBar::tickClock()

{

    clock_->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  hh:mm:ss")));

}



}  // namespace sdv::dash::ui

