#include "ui/GlassPanel.h"

#include "ui/DashboardTheme.h"

#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QVBoxLayout>

namespace sdv::dash::ui {

GlassPanel::GlassPanel(const QString& title, QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("glassPanel"));
    setStyleSheet(DashboardTheme::glassPanelStyle());

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(36);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 180, 255, 55));
    setGraphicsEffect(shadow);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(8);

    title_ = new QLabel(title.toUpper(), this);
    title_->setStyleSheet(QStringLiteral(
        "color: #67e8f9; font-size: 10px; font-weight: 800; letter-spacing: 1.4px; background: transparent;"));
    root->addWidget(title_);

    bodyLayout_ = new QVBoxLayout();
    bodyLayout_->setContentsMargins(0, 0, 0, 0);
    bodyLayout_->setSpacing(6);
    root->addLayout(bodyLayout_, 1);
}

void GlassPanel::setContentWidget(QWidget* widget)
{
    if (content_) {
        bodyLayout_->removeWidget(content_);
        content_->setParent(nullptr);
    }
    content_ = widget;
    if (content_) {
        content_->setParent(this);
        bodyLayout_->addWidget(content_, 1);
    }
}

}  // namespace sdv::dash::ui
