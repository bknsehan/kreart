#include "kreart/ui/mobile/mobile_color_picker.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <string>

#include "kreart/ui/color_picker.h"
#include "kreart/ui/mobile/mobile_theme.h"

namespace kreart::ui::mobile {
namespace {

constexpr double kRingThickness = 28.0;
constexpr double kRingGap = 10.0;
constexpr int kAlphaBarHeight = 24;
constexpr int kAlphaBarGap = 14;

} // namespace

MobileColorPicker::MobileColorPicker(QWidget *parent) : QWidget(parent) {
  // Fixed square layout: the picker's height depends on its width, which
  // heightForWidth cannot express reliably inside a QScrollArea, so the widget
  // keeps a stable height and derives its ring radius from the smaller side.
  setFixedHeight(380);
  setMinimumWidth(280);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMouseTracking(true);
  setCursor(Qt::CrossCursor);
}

QSize MobileColorPicker::sizeHint() const {
  return QSize(360, 380);
}

QPointF MobileColorPicker::ring_center() const {
  const double w = width();
  const double side = std::min(w, static_cast<double>(height()) - kAlphaBarHeight - kAlphaBarGap);
  return QPointF(w * 0.5, side * 0.5);
}

double MobileColorPicker::ring_outer() const {
  const double w = width();
  const double h = height() - kAlphaBarHeight - kAlphaBarGap;
  return std::min(w, h) * 0.5 - 2.0;
}

double MobileColorPicker::ring_inner() const {
  return std::max(20.0, ring_outer() - kRingThickness);
}

QRectF MobileColorPicker::sv_rect() const {
  const QPointF c = ring_center();
  // The square must be inscribed in the ring: its corners sit at
  // half_side * sqrt(2), so cap the half side at inner_radius / sqrt(2) with a
  // little margin, otherwise the corners overlap the hue ring.
  const double half = std::max(20.0, (ring_inner() - kRingGap) / std::sqrt(2.0) * 0.94);
  return QRectF(c.x() - half, c.y() - half, half * 2.0, half * 2.0);
}

QRectF MobileColorPicker::alpha_rect() const {
  const double side = std::min(static_cast<double>(width()), ring_outer() * 2.0);
  const double x = (width() - side) * 0.5;
  const double y = ring_center().y() + ring_outer() + kAlphaBarGap;
  return QRectF(x, y, side, kAlphaBarHeight);
}

core::Rgba8 MobileColorPicker::color() const {
  return StudioColorPicker::hsv_to_rgb(hue_, saturation_, value_,
                                       static_cast<std::uint8_t>(alpha_));
}

void MobileColorPicker::set_color(core::Rgba8 color, bool emit_signal) {
  double h = 0.0, s = 0.0, v = 0.0;
  StudioColorPicker::rgb_to_hsv(color, h, s, v);
  // Preserve the hue when the colour is grey so the ring marker does not jump.
  if (s > 0.001) hue_ = h;
  saturation_ = s;
  value_ = v;
  alpha_ = color.a;
  rebuild_surfaces();
  update();
  if (emit_signal) {
    emit colorChanged(color);
  }
}

void MobileColorPicker::rebuild_surfaces() {
  const QRectF sv = sv_rect();
  const int w = std::max(1, static_cast<int>(sv.width()));
  const int h = std::max(1, static_cast<int>(sv.height()));

  if (std::abs(cached_hue_ - hue_) > 0.5 || sv_image_.size() != QSize(w, h)) {
    cached_hue_ = hue_;
    sv_image_ = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
      const double v = 1.0 - static_cast<double>(y) / std::max(1, h - 1);
      auto *line = reinterpret_cast<QRgb *>(sv_image_.scanLine(y));
      for (int x = 0; x < w; ++x) {
        const double s = static_cast<double>(x) / std::max(1, w - 1);
        const auto c = StudioColorPicker::hsv_to_rgb(hue_, s, v, 255);
        line[x] = qRgb(c.r, c.g, c.b);
      }
    }
  }
}

void MobileColorPicker::paintEvent(QPaintEvent *) {
  rebuild_surfaces();

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const auto &t = tokens();
  const QPointF c = ring_center();
  const double r_out = ring_outer();
  const double r_in = ring_inner();

  // Hue ring
  const int segments = 180;
  for (int i = 0; i < segments; ++i) {
    const double a0 = static_cast<double>(i) / segments * 360.0;
    const double a1 = static_cast<double>(i + 1) / segments * 360.0;
    const auto col = StudioColorPicker::hsv_to_rgb(a0, 1.0, 1.0, 255);
    QPainterPath path;
    path.arcMoveTo(QRectF(c.x() - r_out, c.y() - r_out, r_out * 2, r_out * 2), a0);
    path.arcTo(QRectF(c.x() - r_out, c.y() - r_out, r_out * 2, r_out * 2), a0, a1 - a0);
    path.arcTo(QRectF(c.x() - r_in, c.y() - r_in, r_in * 2, r_in * 2), a1, a0 - a1);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(col.r, col.g, col.b));
    p.drawPath(path);
  }

  // SV square
  const QRectF sv = sv_rect();
  p.save();
  QPainterPath clip;
  clip.addRoundedRect(sv, 12, 12);
  p.setClipPath(clip);
  p.drawImage(sv.topLeft(), sv_image_);
  p.restore();
  p.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(sv, 12, 12);

  // SV marker
  const QPointF sv_marker(sv.left() + saturation_ * sv.width(),
                          sv.top() + (1.0 - value_) * sv.height());
  p.setPen(QPen(Qt::white, 2.0));
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(sv_marker, 9, 9);
  p.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
  p.drawEllipse(sv_marker, 10.5, 10.5);

  // Hue marker
  const double hue_rad = hue_ * M_PI / 180.0;
  const double mid_r = (r_out + r_in) * 0.5;
  const QPointF hue_marker(c.x() + std::cos(hue_rad) * mid_r,
                           c.y() + std::sin(hue_rad) * mid_r);
  p.setPen(QPen(Qt::white, 3.0));
  p.drawEllipse(hue_marker, (r_out - r_in) * 0.5 - 2.0, (r_out - r_in) * 0.5 - 2.0);

  // Alpha bar with checkerboard
  const QRectF ar = alpha_rect();
  p.save();
  QPainterPath alpha_clip;
  alpha_clip.addRoundedRect(ar, 8, 8);
  p.setClipPath(alpha_clip);
  const int check = 8;
  for (int y = static_cast<int>(ar.top()); y < static_cast<int>(ar.bottom()); y += check) {
    for (int x = static_cast<int>(ar.left()); x < static_cast<int>(ar.right()); x += check) {
      const bool alt = ((x / check) + (y / check)) % 2 == 0;
      p.fillRect(QRect(x, y, check, check), alt ? QColor(200, 200, 205) : QColor(150, 150, 156));
    }
  }
  QLinearGradient grad(ar.topLeft(), ar.topRight());
  const auto left_rgba = StudioColorPicker::hsv_to_rgb(hue_, saturation_, value_, 0);
  const auto right_rgba = StudioColorPicker::hsv_to_rgb(hue_, saturation_, value_, 255);
  const QColor left(left_rgba.r, left_rgba.g, left_rgba.b, left_rgba.a);
  const QColor right(right_rgba.r, right_rgba.g, right_rgba.b, right_rgba.a);
  grad.setColorAt(0.0, left);
  grad.setColorAt(1.0, right);
  p.fillRect(ar, grad);
  p.restore();
  p.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(ar, 8, 8);

  const double alpha_x = ar.left() + (alpha_ / 255.0) * ar.width();
  p.setPen(QPen(Qt::white, 2.0));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(QRectF(alpha_x - 4, ar.top() - 2, 8, ar.height() + 4), 4, 4);

  // Magnifier loupe while dragging on the SV square
  if (dragging_ && drag_ == DragTarget::SV) {
    const QPointF focus = drag_pos_;
    const double loupe_r = 44.0;
    QPointF loupe_center = focus + QPointF(0, -loupe_r - 28);
    if (loupe_center.y() - loupe_r < 0) loupe_center = focus + QPointF(0, loupe_r + 28);
    QImage sample = sv_image_.scaled(static_cast<int>(loupe_r * 4), static_cast<int>(loupe_r * 4),
                                     Qt::IgnoreAspectRatio, Qt::FastTransformation);
    const double sx = (focus.x() - sv.left()) / std::max(1.0, sv.width());
    const double sy = (focus.y() - sv.top()) / std::max(1.0, sv.height());
    const QRectF source(sx * sample.width() - loupe_r * 2, sy * sample.height() - loupe_r * 2,
                        loupe_r * 4, loupe_r * 4);
    p.save();
    QPainterPath loupe;
    loupe.addEllipse(loupe_center, loupe_r, loupe_r);
    p.setClipPath(loupe);
    p.drawImage(QRectF(loupe_center.x() - loupe_r, loupe_center.y() - loupe_r,
                       loupe_r * 2, loupe_r * 2),
                sample, source);
    p.restore();
    p.setPen(QPen(Qt::white, 3.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(loupe_center, loupe_r, loupe_r);
    p.setPen(QPen(t.accent, 1.0));
    p.drawEllipse(loupe_center, 4, 4);
  }
}

void MobileColorPicker::update_from_pointer(const QPointF &pos) {
  const QPointF c = ring_center();
  const double dist = std::hypot(pos.x() - c.x(), pos.y() - c.y());
  const double r_out = ring_outer();
  const double r_in = ring_inner();

  bool changed = false;
  if (drag_ == DragTarget::SV) {
    const QRectF sv = sv_rect();
    const double s = std::clamp((pos.x() - sv.left()) / std::max(1.0, sv.width()), 0.0, 1.0);
    const double v = 1.0 - std::clamp((pos.y() - sv.top()) / std::max(1.0, sv.height()), 0.0, 1.0);
    if (std::abs(s - saturation_) > 1e-4 || std::abs(v - value_) > 1e-4) {
      saturation_ = s;
      value_ = v;
      changed = true;
    }
  } else if (drag_ == DragTarget::HUE) {
    double angle = std::atan2(pos.y() - c.y(), pos.x() - c.x()) * 180.0 / M_PI;
    if (angle < 0.0) angle += 360.0;
    if (std::abs(angle - hue_) > 0.2) {
      hue_ = angle;
      changed = true;
    }
  } else if (drag_ == DragTarget::ALPHA) {
    const QRectF ar = alpha_rect();
    const int a = static_cast<int>(std::lround(
        std::clamp((pos.x() - ar.left()) / std::max(1.0, ar.width()), 0.0, 1.0) * 255.0));
    if (a != alpha_) {
      alpha_ = a;
      changed = true;
    }
  }
  (void)dist;
  (void)r_out;
  (void)r_in;

  if (changed) {
    update();
    emit colorChanged(color());
  }
}

void MobileColorPicker::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPointF pos = event->position();
  const QPointF c = ring_center();
  const double dist = std::hypot(pos.x() - c.x(), pos.y() - c.y());
  const double r_out = ring_outer();
  const double r_in = ring_inner();

  if (sv_rect().contains(pos)) {
    drag_ = DragTarget::SV;
  } else if (dist <= r_out && dist >= r_in) {
    drag_ = DragTarget::HUE;
  } else if (alpha_rect().adjusted(-8, -8, 8, 8).contains(pos)) {
    drag_ = DragTarget::ALPHA;
  } else {
    drag_ = DragTarget::NONE;
    QWidget::mousePressEvent(event);
    return;
  }
  dragging_ = true;
  drag_pos_ = pos;
  update_from_pointer(pos);
  update();
  event->accept();
}

void MobileColorPicker::mouseMoveEvent(QMouseEvent *event) {
  if (!dragging_ || drag_ == DragTarget::NONE) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  drag_pos_ = event->position();
  update_from_pointer(drag_pos_);
  event->accept();
}

void MobileColorPicker::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  if (dragging_) {
    dragging_ = false;
    drag_ = DragTarget::NONE;
    update();
    emit colorCommitted(color());
  }
  event->accept();
}

} // namespace kreart::ui::mobile
