#pragma once

#include <QString>

namespace sdv::dash::ui {

struct DashboardTheme {
    static QString applicationStyleSheet();
    static QString glassPanelStyle();
    static QString sidebarStyle();
    static QString topBarStyle();
    static QString hudChipStyle();
    static QString hudSpeedStyle();
    static QString primaryButtonStyle();
    static QString ghostButtonStyle();
    static QString progressBarStyle(const QString& chunkColor = QStringLiteral("#22d3ee"));
    static QString segmentButtonStyle(bool active);
    static QString statusBadgeStyle(const QString& kind);
    static QString featureCardStyle(const QString& accentRgb);
    static QString cockpitFrameStyle();
    static QString miniMapChromeStyle();
    static QString otaPanelStyle();
    static QString eventStatusPillStyle(const QString& status);
};

}  // namespace sdv::dash::ui
