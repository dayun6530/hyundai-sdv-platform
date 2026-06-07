#include "ui/DashboardTheme.h"

namespace sdv::dash::ui {

QString DashboardTheme::applicationStyleSheet()
{
    return QStringLiteral(
        "QMainWindow, QWidget#sdvRoot {"
        "  background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "    stop:0 #03050c, stop:0.45 #050a14, stop:1 #07101f);"
        "  color: #e8f1ff; font-family: 'Segoe UI', 'Malgun Gothic', sans-serif; font-size: 11px;"
        "}"
        "QLabel { background: transparent; color: #dbeafe; }"
        "QComboBox {"
        "  background: rgba(8,18,36,0.88); color: #e2eeff; border: 1px solid rgba(56,189,248,0.45);"
        "  border-radius: 10px; padding: 6px 30px 6px 12px; min-height: 22px;"
        "}"
        "QComboBox::drop-down { border: 0; width: 22px; }"
        "QComboBox QAbstractItemView {"
        "  background: #0a1424; color: #dbeafe; selection-background-color: #12345f; border: 1px solid #245db6;"
        "}"
        "QScrollArea { background: transparent; border: 0; }"
        "QScrollBar:vertical { background: rgba(4,10,20,0.6); width: 8px; margin: 2px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: rgba(56,189,248,0.35); border-radius: 4px; min-height: 32px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QSplitter::handle { background: rgba(34,211,238,0.12); width: 2px; }"
        "QTextEdit, QPlainTextEdit, QListWidget {"
        "  background: rgba(5,12,24,0.72); color: #cbd5e1; border: 1px solid rgba(51,65,85,0.65);"
        "  border-radius: 8px;"
        "}"
        "QTableWidget {"
        "  background: rgba(4,10,20,0.82); alternate-background-color: rgba(8,16,30,0.82);"
        "  color: #b8c7dc; border: 1px solid rgba(34,211,238,0.18); border-radius: 10px; gridline-color: transparent;"
        "}"
        "QHeaderView::section {"
        "  background: rgba(6,14,28,0.95); color: #64748b; border: 0; border-bottom: 1px solid rgba(34,211,238,0.2);"
        "  padding: 6px 8px; font-size: 9px; font-weight: 700; letter-spacing: 0.6px;"
        "}"
        "QTableWidget::item { padding: 4px 8px; border-bottom: 1px solid rgba(15,23,42,0.9); }"
        "QPushButton {"
        "  background: rgba(12,35,64,0.92); color: #e0f2fe; border: 1px solid rgba(34,211,238,0.45);"
        "  border-radius: 10px; padding: 7px 14px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: rgba(18,52,92,0.95); border-color: rgba(56,189,248,0.75); }"
        "QPushButton:disabled { color: #64748b; border-color: #334155; background: rgba(15,23,42,0.6); }");
}

QString DashboardTheme::glassPanelStyle()
{
    return QStringLiteral(
        "QFrame#glassPanel {"
        "  background: rgba(8, 16, 32, 0.62);"
        "  border: 1px solid rgba(56, 189, 248, 0.28);"
        "  border-radius: 16px;"
        "}");
}

QString DashboardTheme::sidebarStyle()
{
    return QStringLiteral(
        "QFrame#navSidebar {"
        "  background: rgba(6, 12, 24, 0.72);"
        "  border: 1px solid rgba(34, 211, 238, 0.14);"
        "  border-radius: 16px;"
        "}"
        "QPushButton#navBtn {"
        "  background: transparent; color: #94a3b8; border: 0; border-radius: 12px;"
        "  padding: 10px 6px; text-align: center; font-size: 10px;"
        "}"
        "QPushButton#navBtn:hover {"
        "  background: rgba(34, 211, 238, 0.08); color: #e0f2fe;"
        "}"
        "QPushButton#navBtn:checked {"
        "  background: rgba(34, 211, 238, 0.14); color: #67e8f9;"
        "  border-left: 3px solid #22d3ee;"
        "}");
}

QString DashboardTheme::topBarStyle()
{
    return QStringLiteral(
        "QFrame#topBar {"
        "  background: rgba(6, 12, 24, 0.55);"
        "  border: 1px solid rgba(34, 211, 238, 0.16);"
        "  border-radius: 14px;"
        "}"
        "QLabel#brandTitle { color: #f1f5f9; font-size: 14px; font-weight: 700; letter-spacing: 0.3px; }"
        "QLabel#brandSub { color: #64748b; font-size: 10px; }"
        "QLabel#clockLabel { color: #94a3b8; font-size: 11px; }");
}

QString DashboardTheme::hudChipStyle()
{
    return QStringLiteral(
        "QFrame#hudChip {"
        "  background: rgba(4, 10, 22, 0.78);"
        "  border: 1px solid rgba(56, 189, 248, 0.38);"
        "  border-radius: 14px;"
        "}");
}

QString DashboardTheme::hudSpeedStyle()
{
    return QStringLiteral(
        "QLabel#speedValue { color: #f8fafc; font-size: 42px; font-weight: 700; }"
        "QLabel#speedUnit { color: #94a3b8; font-size: 14px; font-weight: 600; }");
}

QString DashboardTheme::primaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #0c4a6e, stop:1 #0369a1);"
        "  border: 1px solid #22d3ee; color: #f0f9ff; font-weight: 700; border-radius: 10px; padding: 8px 16px;"
        "}"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #155e75, stop:1 #0284c7); }");
}

QString DashboardTheme::ghostButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  background: rgba(15,23,42,0.75); border: 1px solid #334155; color: #cbd5e1;"
        "  border-radius: 10px; padding: 8px 16px;"
        "}"
        "QPushButton:hover { border-color: #64748b; color: #f1f5f9; }");
}

QString DashboardTheme::progressBarStyle(const QString& chunkColor)
{
    return QStringLiteral(
        "QProgressBar {"
        "  background: rgba(10,18,32,0.9); border: 1px solid rgba(51,65,85,0.7);"
        "  border-radius: 7px; height: 12px; text-align: center; color: #e2e8f0; font-size: 9px;"
        "}"
        "QProgressBar::chunk { background: %1; border-radius: 6px; }")
        .arg(chunkColor);
}

QString DashboardTheme::segmentButtonStyle(const bool active)
{
    if (active) {
        return QStringLiteral(
            "QPushButton { background: rgba(34,211,238,0.22); color: #e0f2fe;"
            " border: 1px solid #22d3ee; border-radius: 8px; padding: 5px 12px; font-weight: 600; }");
    }
    return QStringLiteral(
        "QPushButton { background: transparent; color: #64748b; border: 1px solid transparent;"
        " border-radius: 8px; padding: 5px 12px; }"
        "QPushButton:hover { color: #94a3b8; border-color: rgba(51,65,85,0.8); }");
}

QString DashboardTheme::statusBadgeStyle(const QString& kind)
{
    if (kind == QLatin1String("running")) {
        return QStringLiteral(
            "QLabel { color: #4ade80; background: rgba(4,30,18,0.85);"
            " border: 1px solid rgba(74,222,128,0.55); border-radius: 6px;"
            " padding: 2px 8px; font-size: 9px; font-weight: 800; letter-spacing: 0.8px; }");
    }
    if (kind == QLatin1String("pass")) {
        return QStringLiteral(
            "QLabel { color: #4ade80; background: rgba(4,30,18,0.9);"
            " border: 1px solid rgba(74,222,128,0.6); border-radius: 8px;"
            " padding: 4px 12px; font-size: 11px; font-weight: 800; letter-spacing: 1px; }");
    }
    if (kind == QLatin1String("fail")) {
        return QStringLiteral(
            "QLabel { color: #f87171; background: rgba(40,8,8,0.85);"
            " border: 1px solid rgba(248,113,113,0.55); border-radius: 8px;"
            " padding: 4px 12px; font-size: 11px; font-weight: 800; }");
    }
    return QStringLiteral(
        "QLabel { color: #94a3b8; background: rgba(15,23,42,0.85);"
        " border: 1px solid rgba(100,116,139,0.45); border-radius: 6px;"
        " padding: 2px 8px; font-size: 9px; font-weight: 700; }");
}

QString DashboardTheme::featureCardStyle(const QString& accentRgb)
{
    return QStringLiteral(
        "QFrame#featureCard {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(12,22,40,0.92), stop:1 rgba(6,12,24,0.88));"
        "  border: 1px solid rgba(%1,0.35); border-radius: 12px;"
        "}"
        "QFrame#featureCard:hover {"
        "  border-color: rgba(%1,0.65);"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(16,28,50,0.95), stop:1 rgba(8,16,30,0.9));"
        "}"
        "QLabel { background: transparent; }")
        .arg(accentRgb);
}

QString DashboardTheme::cockpitFrameStyle()
{
    return QStringLiteral(
        "QFrame#cockpitFrame {"
        "  background: #02060f;"
        "  border: 1px solid rgba(56, 189, 248, 0.28);"
        "  border-radius: 18px;"
        "}");
}

QString DashboardTheme::miniMapChromeStyle()
{
    return QStringLiteral(
        "QFrame#miniMapChrome {"
        "  background: rgba(2, 8, 18, 0.35);"
        "  border: 1px solid rgba(56, 189, 248, 0.35);"
        "  border-radius: 14px;"
        "}");
}

QString DashboardTheme::otaPanelStyle()
{
    return QStringLiteral(
        "QLabel#otaMeta { color: #94a3b8; font-size: 9px; font-weight: 600; letter-spacing: 0.4px; }"
        "QLabel#otaValue { color: #f1f5f9; font-size: 12px; font-weight: 700; }"
        "QListWidget { background: rgba(4,10,20,0.75); border: 1px solid rgba(34,211,238,0.15);"
        " border-radius: 8px; color: #94a3b8; font-size: 9px; padding: 4px; }"
        "QListWidget::item { padding: 2px 4px; }"
        "QTextEdit { background: rgba(4,10,20,0.75); border: 1px solid rgba(51,65,85,0.5);"
        " border-radius: 8px; color: #7dd3fc; font-size: 9px; }");
}

QString DashboardTheme::eventStatusPillStyle(const QString& status)
{
    QString bg = QStringLiteral("rgba(15,23,42,0.9)");
    QString fg = QStringLiteral("#94a3b8");
    QString border = QStringLiteral("rgba(100,116,139,0.5)");
    if (status == QLatin1String("SUCCESS")) {
        bg = QStringLiteral("rgba(4,30,18,0.92)");
        fg = QStringLiteral("#4ade80");
        border = QStringLiteral("rgba(74,222,128,0.45)");
    } else if (status == QLatin1String("WARNING") || status == QLatin1String("WARN")) {
        bg = QStringLiteral("rgba(40,32,8,0.92)");
        fg = QStringLiteral("#facc15");
        border = QStringLiteral("rgba(250,204,21,0.45)");
    } else if (status == QLatin1String("ERROR")) {
        bg = QStringLiteral("rgba(40,8,8,0.92)");
        fg = QStringLiteral("#f87171");
        border = QStringLiteral("rgba(248,113,113,0.45)");
    } else if (status == QLatin1String("SHADOW")) {
        bg = QStringLiteral("rgba(8,24,40,0.92)");
        fg = QStringLiteral("#38bdf8");
        border = QStringLiteral("rgba(56,189,248,0.45)");
    }
    return QStringLiteral(
        "QLabel { color: %1; background: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 2px 8px; font-size: 9px; font-weight: 800; letter-spacing: 0.5px; }")
        .arg(fg, bg, border);
}

}  // namespace sdv::dash::ui
