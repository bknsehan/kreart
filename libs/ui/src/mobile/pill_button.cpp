#include "kreart/ui/mobile/pill_button.h"

#include <QPainter>
#include <QPainterPath>

#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_theme.h"

namespace kreart::ui::mobile {

PillButton::PillButton(QWidget *parent) : QPushButton(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setFlat(true);
  apply_style();
}

PillButton::PillButton(const QString &text, QWidget *parent) : PillButton(parent) {
  setText(text);
}

void PillButton::set_variant(Variant variant) {
  variant_ = variant;
  apply_style();
}

void PillButton::set_icon_name(const QString &resource_path) {
  icon_path_ = resource_path;
  const QColor tint = (variant_ == Variant::Accent) ? tokens().accent_ink
                     : (variant_ == Variant::Danger) ? QColor(26, 11, 11)
                                                     : tokens().text;
  setIcon(IconHelper::load_symbolic(resource_path, tint, 22));
  setIconSize(QSize(22, 22));
}

void PillButton::set_badge(int count) {
  badge_ = count;
  update();
}

void PillButton::set_circular(bool circular) {
  circular_ = circular;
  setFixedSize(touch_size_, touch_size_);
  apply_style();
}

void PillButton::set_touch_size(int size) {
  touch_size_ = std::max(48, size);
  if (circular_) {
    setFixedSize(touch_size_, touch_size_);
  } else {
    setMinimumHeight(touch_size_);
  }
  apply_style();
}

void PillButton::apply_style() {
  const auto &t = tokens();
  QColor bg = t.surface_raised;
  QColor fg = t.text;
  switch (variant_) {
    case Variant::Ghost:
      bg = Qt::transparent;
      fg = t.text_muted;
      break;
    case Variant::Surface:
      bg = t.surface_raised;
      fg = t.text;
      break;
    case Variant::Accent:
      bg = t.accent;
      fg = t.accent_ink;
      break;
    case Variant::Danger:
      bg = t.danger;
      fg = QColor(26, 11, 11);
      break;
  }

  const int radius = circular_ ? touch_size_ / 2 : t.radius_control;
  const QColor pressed = (variant_ == Variant::Accent) ? t.accent_pressed
                         : (variant_ == Variant::Danger) ? QColor(200, 70, 70)
                                                         : QColor(42, 46, 54);
  setStyleSheet(QString(R"(
QPushButton {
  background: %1;
  color: %2;
  border: none;
  border-radius: %3px;
  font-size: 14px;
  font-weight: %4;
  padding: 0 10px;
}
QPushButton:pressed { background: %5; }
QPushButton:disabled { color: %6; background: transparent; }
)")
                    .arg(bg.alpha() == 0 ? QStringLiteral("transparent") : css_color(bg))
                    .arg(css_color(fg))
                    .arg(radius)
                    .arg(variant_ == Variant::Accent ? 600 : 400)
                    .arg(css_color(pressed))
                    .arg(css_color(t.text_muted)));

  if (circular_) {
    setFixedSize(touch_size_, touch_size_);
  } else {
    setMinimumSize(touch_size_, touch_size_);
  }
  if (!icon_path_.isEmpty()) {
    const QColor tint = (variant_ == Variant::Accent) ? t.accent_ink : fg;
    setIcon(IconHelper::load_symbolic(icon_path_, tint, 22));
    setIconSize(QSize(22, 22));
  }
}

QSize PillButton::sizeHint() const {
  if (circular_) return QSize(touch_size_, touch_size_);
  QSize base = QPushButton::sizeHint();
  return QSize(std::max(base.width(), touch_size_), std::max(base.height(), touch_size_));
}

void PillButton::paintEvent(QPaintEvent *event) {
  QPushButton::paintEvent(event);
  if (badge_ <= 0) return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  const QString text = badge_ > 99 ? QStringLiteral("99+") : QString::number(badge_);
  QFont f = font_caption();
  f.setWeight(QFont::DemiBold);
  p.setFont(f);
  const QFontMetrics fm(f);
  const int w = std::max(18, fm.horizontalAdvance(text) + 10);
  const int h = 18;
  const QRect r(width() - w - 2, 2, w, h);
  p.setPen(Qt::NoPen);
  p.setBrush(tokens().accent);
  p.drawRoundedRect(r, h / 2.0, h / 2.0);
  p.setPen(tokens().accent_ink);
  p.drawText(r, Qt::AlignCenter, text);
}

} // namespace kreart::ui::mobile
