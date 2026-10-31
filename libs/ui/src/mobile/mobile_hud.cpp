#include "kreart/ui/mobile/mobile_hud.h"

#include <QFontMetrics>
#include <QPainter>

#include "kreart/ui/mobile/mobile_theme.h"

namespace kreart::ui::mobile {

MobileHud::MobileHud(QWidget *host) : QWidget(host), host_(host) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setFocusPolicy(Qt::NoFocus);
  hide();

  hide_timer_.setSingleShot(true);
  connect(&hide_timer_, &QTimer::timeout, this, [this]() { hide_now(); });
}

void MobileHud::set_anchor(int center_x, int bottom_y) {
  anchor_x_ = center_x;
  anchor_y_ = bottom_y;
  if (isVisible()) {
    // Force a re-layout of the chip at the new anchor.
    const QString text = text_;
    show_message(text, 0);
  }
}

void MobileHud::show_message(const QString &text, int ms) {
  text_ = text;
  QFont f = font_caption();
  const QFontMetrics fm(f);
  const int w = std::min(static_cast<int>(fm.horizontalAdvance(text)) + 36, 360);
  const int h = 40;

  int x = 0;
  int y = 0;
  if (host_) {
    x = (anchor_x_ >= 0) ? anchor_x_ - w / 2 : (host_->width() - w) / 2;
    y = (anchor_y_ >= 0) ? anchor_y_ - h : host_->height() - h - 96;
    x = std::clamp(x, 12, std::max(12, host_->width() - w - 12));
    y = std::clamp(y, 12, std::max(12, host_->height() - h - 12));
  }
  setGeometry(x, y, w, h);
  raise();
  show();
  update();
  if (ms > 0) {
    hide_timer_.start(ms);
  } else {
    hide_timer_.stop();
  }
}

void MobileHud::hide_now() {
  hide_timer_.stop();
  hide();
}

void MobileHud::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  QColor bg = tokens().surface_raised;
  bg.setAlphaF(0.92);
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), height() / 2.0, height() / 2.0);

  p.setFont(font_caption());
  p.setPen(tokens().text);
  p.drawText(rect().adjusted(16, 0, -16, 0), Qt::AlignCenter,
             QFontMetrics(font_caption()).elidedText(text_, Qt::ElideRight, width() - 32));
}

} // namespace kreart::ui::mobile
