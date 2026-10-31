#include "kreart/ui/desktop_theme.h"
#include "kreart/ui/app_settings.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QString>

namespace kreart::ui {
namespace {

bool resolve_dark() {
  switch (AppSettings::instance().theme()) {
    case ThemeMode::DARK:
      return true;
    case ThemeMode::LIGHT:
      return false;
    case ThemeMode::SYSTEM:
    default:
      break;
  }
  const QStyleHints *hints = QGuiApplication::styleHints();
  if (hints == nullptr) {
    return true;
  }
  return hints->colorScheme() != Qt::ColorScheme::Light;
}

} // namespace

void apply_desktop_theme() {
  QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (app == nullptr) {
    return;
  }

  const bool dark = resolve_dark();
  const QColor highlight_color(dark ? "#3584e4" : "#2563eb");
  const QColor text_color(dark ? "#e4e5e7" : "#1a1c21");
  const QColor text_muted(dark ? "#909296" : "#70737a");
  const QColor window_bg(dark ? "#1a1b1e" : "#f2f3f5");
  const QColor base_bg(dark ? "#141517" : "#ffffff");
  const QColor button_bg(dark ? "#25262b" : "#e6e8eb");
  const QColor border_color(dark ? "#2c2e33" : "#d0d3d8");
  const QColor menu_bg(dark ? "#1e1f23" : "#ffffff");

  QPalette pal;
  if (dark) {
    pal.setColor(QPalette::Window, window_bg);
    pal.setColor(QPalette::WindowText, text_color);
    pal.setColor(QPalette::Base, base_bg);
    pal.setColor(QPalette::AlternateBase, QColor("#1e2024"));
    pal.setColor(QPalette::Text, text_color);
    pal.setColor(QPalette::Button, button_bg);
    pal.setColor(QPalette::ButtonText, text_color);
    pal.setColor(QPalette::ToolTipBase, base_bg);
    pal.setColor(QPalette::ToolTipText, text_color);
    pal.setColor(QPalette::Mid, border_color);
    pal.setColor(QPalette::Midlight, QColor("#2b2e35"));
    pal.setColor(QPalette::Dark, QColor("#0d0e11"));
    pal.setColor(QPalette::Shadow, QColor("#000000"));
    pal.setColor(QPalette::Highlight, highlight_color);
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::Link, highlight_color);
    pal.setColor(QPalette::PlaceholderText, text_muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#666970"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#666970"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#666970"));
  } else {
    pal.setColor(QPalette::Window, window_bg);
    pal.setColor(QPalette::WindowText, text_color);
    pal.setColor(QPalette::Base, base_bg);
    pal.setColor(QPalette::AlternateBase, QColor("#f6f7f9"));
    pal.setColor(QPalette::Text, text_color);
    pal.setColor(QPalette::Button, button_bg);
    pal.setColor(QPalette::ButtonText, text_color);
    pal.setColor(QPalette::ToolTipBase, base_bg);
    pal.setColor(QPalette::ToolTipText, text_color);
    pal.setColor(QPalette::Mid, border_color);
    pal.setColor(QPalette::Midlight, QColor("#dfe2e7"));
    pal.setColor(QPalette::Dark, QColor("#a8adb8"));
    pal.setColor(QPalette::Shadow, QColor("#7b818d"));
    pal.setColor(QPalette::Highlight, highlight_color);
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::Link, highlight_color);
    pal.setColor(QPalette::PlaceholderText, text_muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#8f959f"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#8f959f"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8f959f"));
  }

  app->setPalette(pal);

  const QString checked_bg = dark ? "rgba(53, 132, 228, 0.28)" : "rgba(37, 99, 235, 0.22)";
  const QString hover_bg = dark ? "rgba(255, 255, 255, 0.08)" : "rgba(0, 0, 0, 0.06)";
  const QString sel_bg = dark ? "rgba(53, 132, 228, 0.25)" : "rgba(37, 99, 235, 0.18)";
  const QString groove_bg = dark ? "rgba(255, 255, 255, 0.15)" : "rgba(0, 0, 0, 0.15)";
  const QString handle_bg = dark ? "#d0d2d6" : "#555860";
  const QString scroll_h = dark ? "rgba(255, 255, 255, 0.15)" : "rgba(0, 0, 0, 0.15)";
  const QString scroll_hover = dark ? "rgba(255, 255, 255, 0.28)" : "rgba(0, 0, 0, 0.28)";

  const QString qss = QString(
      "QTabBar::tab { background: transparent; border: none; padding: 7px 14px; color: %1; font-weight: 500; }\n"
      "QTabBar::tab:hover { background: transparent; color: %2; }\n"
      "QTabBar::tab:selected { background: transparent; border-bottom: 2px solid %3; color: %2; }\n"
      "QPushButton { border-radius: 4px; padding: 5px 12px; }\n"
      "QPushButton:checked, QToolButton:checked { background: %4; color: #ffffff; }\n"
      "QPushButton:hover:!checked, QToolButton:hover:!checked { background: %5; }\n"
      "QSlider::groove:horizontal { height: 4px; background: %6; border-radius: 2px; }\n"
      "QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }\n"
      "QSlider::add-page:horizontal { background: %6; border-radius: 2px; }\n"
      "QSlider::handle:horizontal { background: %7; width: 14px; margin: -5px 0; border-radius: 7px; }\n"
      "QSlider::handle:horizontal:hover { background: #ffffff; }\n"
      "QProgressBar::chunk { background: %3; border-radius: 2px; }\n"
      "QTreeView::item:selected, QListView::item:selected, QListWidget::item:selected, QTableView::item:selected { background: %8; color: %2; }\n"
      "QMenu { background-color: %9; border: 1px solid %10; padding: 4px 0px; border-radius: 6px; }\n"
      "QMenu::item { padding: 6px 22px; color: %2; }\n"
      "QMenu::item:selected { background: %8; color: #ffffff; }\n"
      "QMenu::separator { height: 1px; background: %10; margin: 4px 8px; }\n"
      "QComboBox:focus, QLineEdit:focus, QSpinBox:focus { border: 1px solid %3; }\n"
      "QComboBox QAbstractItemView { background: %9; border: 1px solid %10; selection-background-color: %8; selection-color: %2; }\n"
      "QScrollBar:vertical { width: 8px; background: transparent; margin: 0; }\n"
      "QScrollBar::handle:vertical { background: %11; min-height: 20px; border-radius: 4px; }\n"
      "QScrollBar::handle:vertical:hover { background: %12; }\n"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }\n"
      "QScrollBar:horizontal { height: 8px; background: transparent; margin: 0; }\n"
      "QScrollBar::handle:horizontal { background: %11; min-width: 20px; border-radius: 4px; }\n"
      "QScrollBar::handle:horizontal:hover { background: %12; }\n"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }\n"
      "QToolTip { background: %9; color: %2; border: 1px solid %10; border-radius: 4px; }\n"
  ).arg(text_muted.name(), text_color.name(), highlight_color.name(),
       checked_bg, hover_bg, groove_bg, handle_bg, sel_bg, menu_bg.name(),
       border_color.name(), scroll_h, scroll_hover);

  app->setStyleSheet(qss);
}

} // namespace kreart::ui
