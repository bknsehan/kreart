#include "kreart/ui/mobile/quick_wheel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

#include <cmath>

#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_theme.h"

namespace kreart::ui::mobile {
namespace {

constexpr double kOuterRadius = 150.0;
constexpr double kInnerRadius = 52.0;
constexpr double kCenterRadius = 42.0;
constexpr int kSectorCount = 8;

} // namespace

QuickWheel::QuickWheel(QWidget *host) : QWidget(host), host_(host) {
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setFocusPolicy(Qt::StrongFocus);
  setFixedSize(kWheelSize, kWheelSize);
  hide();
}

QString QuickWheel::sector_label(Sector sector) const {
  switch (sector) {
    case Sector::UNDO: return tr("Undo");
    case Sector::REDO: return tr("Redo");
    case Sector::ERASER: return eraser_active_ ? tr("Brush") : tr("Eraser");
    case Sector::FIT: return tr("Fit");
    case Sector::MIRROR: return tr("Mirror");
    case Sector::RESET_ROTATION: return tr("Reset");
    case Sector::SIZE_UP: return tr("Size +");
    case Sector::SIZE_DOWN: return tr("Size -");
    default: return {};
  }
}

QString QuickWheel::sector_icon(Sector sector) const {
  switch (sector) {
    case Sector::UNDO: return ":/icons/icons/edit-undo-symbolic.svg";
    case Sector::REDO: return ":/icons/icons/edit-redo-symbolic.svg";
    case Sector::ERASER: return ":/icons/icons/tool-eraser-symbolic.svg";
    case Sector::FIT: return ":/icons/icons/pan-end-symbolic.svg";
    case Sector::MIRROR: return ":/icons/icons/symmetry-h-symbolic.svg";
    case Sector::RESET_ROTATION: return ":/icons/icons/edit-redo-symbolic.svg";
    case Sector::SIZE_UP: return ":/icons/icons/list-add-symbolic.svg";
    case Sector::SIZE_DOWN: return ":/icons/icons/list-remove-symbolic.svg";
    default: return {};
  }
}

void QuickWheel::open_at(const QPointF &host_pos) {
  if (!host_) return;
  const int host_w = host_->width();
  const int host_h = host_->height();
  if (host_w <= 0 || host_h <= 0) return;

  const int margin = 24;
  int x = static_cast<int>(host_pos.x()) - kWheelSize / 2;
  int y = static_cast<int>(host_pos.y()) - kWheelSize / 2;
  x = std::clamp(x, margin, std::max(margin, host_w - kWheelSize - margin));
  y = std::clamp(y, margin, std::max(margin, host_h - kWheelSize - margin));

  setGeometry(x, y, kWheelSize, kWheelSize);
  center_ = QPointF(kWheelSize / 2.0, kWheelSize / 2.0);
  hover_ = Sector::NONE;
  is_open_ = true;
  show();
  raise();
  setFocus(Qt::OtherFocusReason);
  update();
}

void QuickWheel::close_wheel() {
  if (!is_open_) return;
  is_open_ = false;
  hover_ = Sector::NONE;
  hide();
}

void QuickWheel::set_center_color(const QColor &color) {
  center_color_ = color;
  update();
}

void QuickWheel::set_eraser_active(bool active) {
  eraser_active_ = active;
  update();
}

QRectF QuickWheel::center_disc() const {
  return QRectF(center_.x() - kCenterRadius, center_.y() - kCenterRadius,
                kCenterRadius * 2.0, kCenterRadius * 2.0);
}

QuickWheel::Sector QuickWheel::sector_at(const QPointF &pos) const {
  const double dx = pos.x() - center_.x();
  const double dy = pos.y() - center_.y();
  const double dist = std::hypot(dx, dy);
  if (dist < kInnerRadius || dist > kOuterRadius) return Sector::NONE;

  // Sector 0 is centred at the top; each sector spans 45 degrees.
  double angle = std::atan2(dy, dx) * 180.0 / M_PI;  // -180..180, 0 = right
  angle += 90.0;                                     // 0 = up
  if (angle < 0.0) angle += 360.0;
  const int idx = static_cast<int>((angle + 22.5) / 45.0) % kSectorCount;
  return static_cast<Sector>(idx);
}

void QuickWheel::activate(Sector sector) {
  switch (sector) {
    case Sector::UNDO: emit undoRequested(); break;
    case Sector::REDO: emit redoRequested(); break;
    case Sector::ERASER: emit toggleEraserRequested(); break;
    case Sector::FIT: emit zoomFitRequested(); break;
    case Sector::MIRROR: emit mirrorRequested(); break;
    case Sector::RESET_ROTATION: emit resetRotationRequested(); break;
    case Sector::SIZE_UP: emit brushSizeIncreaseRequested(); break;
    case Sector::SIZE_DOWN: emit brushSizeDecreaseRequested(); break;
    default: break;
  }
  close_wheel();
}

void QuickWheel::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const auto &t = tokens();
  const QPointF c = center_;

  // Backdrop disc
  QRadialGradient bg(c, kOuterRadius);
  bg.setColorAt(0.0, QColor(24, 26, 31, 246));
  bg.setColorAt(1.0, QColor(16, 17, 21, 238));
  p.setPen(QPen(t.hairline, 1.0));
  p.setBrush(bg);
  p.drawEllipse(c, kOuterRadius, kOuterRadius);

  // Sectors
  for (int i = 0; i < kSectorCount; ++i) {
    const auto sector = static_cast<Sector>(i);
    const double start_deg = -90.0 - 22.5 + i * 45.0;
    const bool active = (hover_ == sector);
    const QRectF arc_rect(c.x() - kOuterRadius + 6.0, c.y() - kOuterRadius + 6.0,
                          (kOuterRadius - 6.0) * 2.0, (kOuterRadius - 6.0) * 2.0);
    if (active) {
      p.setPen(Qt::NoPen);
      QColor hi = t.accent;
      hi.setAlpha(60);
      p.setBrush(hi);
      p.drawPie(arc_rect, static_cast<int>((start_deg) * 16.0), 45 * 16);
    }

    // Icon and label placed on the sector's mid-radius.
    const double mid_deg = start_deg + 22.5;
    const double rad = mid_deg * M_PI / 180.0;
    const double r = (kOuterRadius + kInnerRadius) * 0.5;
    const QPointF icon_pos(c.x() + std::cos(rad) * r, c.y() + std::sin(rad) * r);

    const QColor tint = active ? t.accent : t.text;
    const QPixmap icon = IconHelper::render_symbolic(sector_icon(sector), tint, 24);
    if (!icon.isNull()) {
      p.drawPixmap(QPointF(icon_pos.x() - 12.0, icon_pos.y() - 18.0), icon);
    }
    QFont f = font_caption();
    f.setWeight(active ? QFont::DemiBold : QFont::Normal);
    p.setFont(f);
    p.setPen(active ? t.accent : t.text_muted);
    const QRectF label_rect(icon_pos.x() - 40.0, icon_pos.y() + 6.0, 80.0, 16.0);
    p.drawText(label_rect, Qt::AlignHCenter | Qt::AlignTop, sector_label(sector));
  }

  // Inner ring separator
  p.setPen(QPen(t.hairline, 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(c, kInnerRadius, kInnerRadius);

  // Center disc = current colour / colour shortcut. A pale inner ring keeps the
  // disc visible even when the current colour is near-black.
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(200, 205, 215));
  p.drawEllipse(c, kCenterRadius - 1.0, kCenterRadius - 1.0);
  p.setBrush(center_color_);
  p.drawEllipse(c, kCenterRadius - 4.0, kCenterRadius - 4.0);
  p.setPen(QPen(QColor(255, 255, 255, 60), 1.5));
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(c, kCenterRadius - 2.0, kCenterRadius - 2.0);
}

void QuickWheel::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPointF pos = event->position();
  if (QLineF(center_, pos).length() <= kCenterRadius) {
    emit colorRequested();
    close_wheel();
    event->accept();
    return;
  }
  const Sector s = sector_at(pos);
  if (s == Sector::NONE) {
    // Tap outside the ring dismisses.
    close_wheel();
    event->accept();
    return;
  }
  hover_ = s;
  update();
  event->accept();
}

void QuickWheel::mouseMoveEvent(QMouseEvent *event) {
  const Sector s = sector_at(event->position());
  if (s != hover_) {
    hover_ = s;
    update();
  }
  event->accept();
}

void QuickWheel::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  const Sector s = sector_at(event->position());
  if (s != Sector::NONE) {
    activate(s);
  } else {
    close_wheel();
  }
  event->accept();
}

void QuickWheel::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Back) {
    close_wheel();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

} // namespace kreart::ui::mobile
