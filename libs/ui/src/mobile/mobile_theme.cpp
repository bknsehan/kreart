#include "kreart/ui/mobile/mobile_theme.h"

#include <cmath>

namespace kreart::ui::mobile {
namespace {

Tokens &mutable_tokens() {
  static Tokens t;
  return t;
}

} // namespace

const Tokens &tokens() {
  return mutable_tokens();
}

void set_accent_colors(const QColor &accent, const QColor &pressed, const QColor &ink) {
  auto &t = mutable_tokens();
  if (accent.isValid()) t.accent = accent;
  if (pressed.isValid()) t.accent_pressed = pressed;
  if (ink.isValid()) t.accent_ink = ink;
}

void reset_accent_colors() {
  auto &t = mutable_tokens();
  t.accent = QColor(232, 180, 74);
  t.accent_pressed = QColor(198, 148, 52);
  t.accent_ink = QColor(26, 20, 8);
}

QFont font_title() {
  QFont f;
  f.setPixelSize(20);
  f.setWeight(QFont::DemiBold);
  return f;
}

QFont font_section() {
  QFont f;
  f.setPixelSize(15);
  f.setWeight(QFont::DemiBold);
  return f;
}

QFont font_body() {
  QFont f;
  f.setPixelSize(14);
  return f;
}

QFont font_caption() {
  QFont f;
  f.setPixelSize(12);
  return f;
}

QString css_color(const QColor &color, bool with_alpha) {
  if (with_alpha) {
    return QString("rgba(%1, %2, %3, %4%)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(static_cast<int>(std::lround(color.alphaF() * 100.0)));
  }
  return QString("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
}

QString sheet_qss() {
  const auto &t = tokens();
  const QString accent = css_color(t.accent);
  const QString accent_pressed = css_color(t.accent_pressed);
  const QString accent_ink = css_color(t.accent_ink);
  const QString surface = css_color(t.surface);
  const QString raised = css_color(t.surface_raised);
  const QString text = css_color(t.text);
  const QString muted = css_color(t.text_muted);
  const QString danger = css_color(t.danger);
  const QString hairline = css_color(t.hairline, true);

  // Every rule is scoped to QWidget#MobileSheet so the desktop UI and the
  // mobile canvas/gallery are never restyled by accident.
  return QString(R"(
QWidget#MobileSheet {
  background: %1;
  border-top-left-radius: %2px;
  border-top-right-radius: %2px;
  border-bottom-left-radius: 0px;
  border-bottom-right-radius: 0px;
}
QWidget#MobileSheet QLabel {
  color: %3;
  background: transparent;
}
QWidget#MobileSheet QLabel[muted="true"] {
  color: %4;
}
QWidget#MobileSheet QPushButton {
  min-height: 44px;
  border-radius: %5px;
  border: none;
  background: %6;
  color: %3;
  padding: 0 16px;
  font-size: 14px;
}
QWidget#MobileSheet QPushButton:pressed {
  background: #2A2E36;
}
QWidget#MobileSheet QPushButton:disabled {
  color: %4;
  background: #1B1D22;
}
QWidget#MobileSheet QPushButton[accent="true"] {
  background: %7;
  color: %8;
  font-weight: 600;
}
QWidget#MobileSheet QPushButton[accent="true"]:pressed {
  background: %9;
}
QWidget#MobileSheet QPushButton[danger="true"] {
  background: %10;
  color: #1A0B0B;
  font-weight: 600;
}
QWidget#MobileSheet QPushButton[ghost="true"] {
  background: transparent;
  color: %4;
}
QWidget#MobileSheet QPushButton[tile="true"] {
  min-height: 72px;
  border-radius: %11px;
  background: %6;
  text-align: center;
  padding: 0 8px;
}
QWidget#MobileSheet QSlider::groove:horizontal {
  height: 6px;
  border-radius: 3px;
  background: #2A2E36;
}
QWidget#MobileSheet QSlider::sub-page:horizontal {
  height: 6px;
  border-radius: 3px;
  background: %7;
}
QWidget#MobileSheet QSlider::handle:horizontal {
  width: 26px;
  height: 26px;
  margin: -10px 0;
  border-radius: 13px;
  background: %7;
}
QWidget#MobileSheet QScrollBar:vertical,
QWidget#MobileSheet QScrollBar:horizontal {
  width: 0px;
  height: 0px;
  background: transparent;
}
QWidget#MobileSheet QScrollArea,
QWidget#MobileSheet QScrollArea > QWidget > QWidget {
  background: transparent;
  border: none;
}
QWidget#MobileSheet QScrollArea > QWidget > QWidget > QWidget {
  background: transparent;
}
QWidget#MobileSheet QAbstractScrollArea::corner {
  background: transparent;
}
QWidget#MobileSheet QComboBox {
  min-height: 44px;
  border-radius: %5px;
  background: %6;
  color: %3;
  padding: 0 12px;
  border: 1px solid %12;
}
QWidget#MobileSheet QComboBox QAbstractItemView {
  background: %6;
  color: %3;
  selection-background-color: %7;
  selection-color: %8;
  border: 1px solid %12;
}
QWidget#MobileSheet QLineEdit {
  min-height: 44px;
  border-radius: %5px;
  background: %6;
  color: %3;
  padding: 0 12px;
  border: 1px solid %12;
  selection-background-color: %7;
  selection-color: %8;
}
QWidget#MobileSheet QCheckBox {
  color: %3;
  spacing: 10px;
  min-height: 44px;
}
QWidget#MobileSheet QCheckBox::indicator {
  width: 22px;
  height: 22px;
  border-radius: 6px;
  border: 2px solid #3A3F49;
  background-color: #1A1D22;
}
QWidget#MobileSheet QCheckBox::indicator:hover {
  border: 2px solid #4A505C;
}
QWidget#MobileSheet QCheckBox::indicator:checked {
  border: 2px solid %7;
  background-color: %7;
}
QWidget#MobileSheet QCheckBox::indicator:checked:hover {
  border: 2px solid %9;
  background-color: %9;
}
QWidget#MobileSheet QListWidget {
  background: transparent;
  border: none;
  outline: none;
}
QWidget#MobileSheet QListWidget::item {
  min-height: 52px;
  border-radius: %5px;
}
QWidget#MobileSheet QListWidget::item:selected {
  background: #262A31;
}
QWidget#MobileSheet QTabBar::tab {
  min-height: 44px;
  padding: 0 16px;
  background: transparent;
  color: %4;
}
QWidget#MobileSheet QTabBar::tab:selected {
  color: %7;
  border-bottom: 2px solid %7;
}
QWidget#MobileSheet QScrollBar::handle {
  background: transparent;
}
)")
      .arg(surface)
      .arg(t.radius_sheet)
      .arg(text)
      .arg(muted)
      .arg(t.radius_control)
      .arg(raised)
      .arg(accent)
      .arg(accent_ink)
      .arg(accent_pressed)
      .arg(danger)
      .arg(t.radius_card)
      .arg(hairline);
}

} // namespace kreart::ui::mobile
