#include "app_palette.h"

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace dashboard {

void applyDarkPalette(QApplication &app) {
    app.setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(30, 30, 30));
    palette.setColor(QPalette::WindowText, QColor(212, 212, 212));
    palette.setColor(QPalette::Base, QColor(24, 24, 24));
    palette.setColor(QPalette::AlternateBase, QColor(34, 34, 34));
    palette.setColor(QPalette::ToolTipBase, QColor(40, 40, 40));
    palette.setColor(QPalette::ToolTipText, QColor(212, 212, 212));
    palette.setColor(QPalette::Text, QColor(212, 212, 212));
    palette.setColor(QPalette::Button, QColor(45, 45, 45));
    palette.setColor(QPalette::ButtonText, QColor(212, 212, 212));
    palette.setColor(QPalette::BrightText, QColor(255, 85, 85));
    palette.setColor(QPalette::Link, QColor(86, 156, 214));
    palette.setColor(QPalette::Highlight, QColor(0, 122, 204));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(90, 90, 90));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(90, 90, 90));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(90, 90, 90));

    app.setPalette(palette);

    app.setStyleSheet(QStringLiteral(
        "QGroupBox {"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 6px;"
        "  margin-top: 14px;"
        "  padding: 12px 8px 8px 8px;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  padding: 2px 10px;"
        "  color: #569CD6;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 4px;"
        "  top: -1px;"
        "}"
        "QTabBar::tab {"
        "  background: #2d2d2d;"
        "  border: 1px solid #3c3c3c;"
        "  border-bottom: none;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  padding: 8px 24px;"
        "  margin-right: 2px;"
        "  color: #999;"
        "  font-size: 12px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #1e1e1e;"
        "  color: #d4d4d4;"
        "  border-bottom: 2px solid #007ACC;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "  background: #383838;"
        "  color: #d4d4d4;"
        "}"
        "QTableWidget {"
        "  gridline-color: #2a2a2a;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 4px;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 11px;"
        "}"
        "QTableWidget::item {"
        "  padding: 4px 6px;"
        "}"
        "QTableWidget::item:selected {"
        "  background: #264f78;"
        "  color: #ffffff;"
        "}"
        "QHeaderView::section {"
        "  background: #2d2d2d;"
        "  color: #569CD6;"
        "  border: none;"
        "  border-bottom: 2px solid #007ACC;"
        "  padding: 6px 8px;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
        "QPushButton {"
        "  background: #0e639c;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background: #1177bb;"
        "}"
        "QPushButton:pressed {"
        "  background: #0d5689;"
        "}"
        "QComboBox {"
        "  background: #3c3c3c;"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  color: #d4d4d4;"
        "  font-size: 11px;"
        "}"
        "QComboBox::drop-down {"
        "  border: none;"
        "  width: 20px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: #2d2d2d;"
        "  border: 1px solid #555;"
        "  selection-background-color: #264f78;"
        "}"
        "QScrollBar:vertical {"
        "  background: #1e1e1e;"
        "  width: 10px;"
        "  border: none;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #424242;"
        "  border-radius: 5px;"
        "  min-height: 20px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #555;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar:horizontal {"
        "  background: #1e1e1e;"
        "  height: 10px;"
        "  border: none;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: #424242;"
        "  border-radius: 5px;"
        "  min-width: 20px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "  background: #555;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "}"
        "QStatusBar {"
        "  background: #007ACC;"
        "  color: #ffffff;"
        "  font-size: 11px;"
        "}"
        "QStatusBar QLabel {"
        "  color: #ffffff;"
        "  padding: 2px 8px;"
        "}"
        "QPlainTextEdit {"
        "  background-color: #181818;"
        "  color: #d4d4d4;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 4px;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 10px;"
        "  selection-background-color: #264f78;"
        "}"
    ));
}

}  // namespace dashboard
