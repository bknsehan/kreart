#include "kreart/ui/pressure_curve.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace kreart::ui {
namespace {

constexpr double kGraphPadding = 9.0;
constexpr double kMinGamma = 0.15;
constexpr double kMaxGamma = 6.0;

} // namespace

PressureCurve::PressureCurve(QWidget *parent)
    : QWidget(parent) {
  setCursor(Qt::SizeVerCursor);
  setToolTip(tr("Drag vertically to shape how pen pressure affects brush size and flow"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(104);
}

QSize PressureCurve::sizeHint() const {
  return QSize(200, 104);
}

void PressureCurve::set_gamma(double gamma) {
  const double clamped = std::clamp(gamma, kMinGamma, kMaxGamma);
  if (std::abs(clamped - gamma_) < 0.0005) {
    return;
  }
  gamma_ = clamped;
  update();
}

void PressureCurve::reset() {
  gamma_ = 1.0;
  update();
  emit curveChanged(gamma_);
}

void PressureCurve::set_gamma_from_pointer(double widget_y) {
  const double graph_height = static_cast<double>(height()) - 2.0 * kGraphPadding;
  if (graph_height <= 1.0) {
    return;
  }

  const double vertical = std::clamp((widget_y - kGraphPadding) / graph_height, 0.0, 1.0);
  const double response = std::clamp(1.0 - vertical, 0.015, 0.985);
  const double gamma = std::clamp(-std::log2(response), kMinGamma, kMaxGamma);
  if (std::abs(gamma - gamma_) < 0.0005) {
    return;
  }

  gamma_ = gamma;
  update();
  emit curveChanged(gamma_);
}

void PressureCurve::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = true;
    set_gamma_from_pointer(event->position().y());
  }
}

void PressureCurve::mouseMoveEvent(QMouseEvent *event) {
  if (dragging_) {
    set_gamma_from_pointer(event->position().y());
  }
}

void PressureCurve::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
  }
}

void PressureCurve::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  const double w = width();
  const double h = height();

  // Background and border from theme palette
  QColor bg = palette().color(QPalette::Base);
  QColor border = palette().color(QPalette::Mid);
  QColor accent = palette().color(QPalette::Highlight);
  QColor textMuted = palette().color(QPalette::PlaceholderText);

  QRectF outerRect(0.5, 0.5, w - 1.0, h - 1.0);
  p.setPen(QPen(border, 1.0));
  p.setBrush(bg);
  p.drawRoundedRect(outerRect, 7.0, 7.0);

  const double left = kGraphPadding;
  const double top = kGraphPadding;
  const double graph_width = std::max(1.0, w - 2.0 * kGraphPadding);
  const double graph_height = std::max(1.0, h - 2.0 * kGraphPadding);

  p.save();
  p.setClipRect(QRectF(left, top, graph_width, graph_height));

  // Grid lines
  QColor gridColor = textMuted;
  gridColor.setAlpha(60);
  p.setPen(QPen(gridColor, 1.0));
  for (int i = 1; i < 4; ++i) {
    const double x = left + graph_width * static_cast<double>(i) / 4.0;
    const double y = top + graph_height * static_cast<double>(i) / 4.0;
    p.drawLine(QPointF(x, top), QPointF(x, top + graph_height));
    p.drawLine(QPointF(left, y), QPointF(left + graph_width, y));
  }

  // Linear diagonal guide
  QPen dashPen(gridColor, 1.0, Qt::DashLine);
  p.setPen(dashPen);
  p.drawLine(QPointF(left, top + graph_height), QPointF(left + graph_width, top));

  // Gamma curve
  p.setPen(QPen(accent, 2.2));
  QPainterPath path;
  constexpr int kSegments = 64;
  for (int i = 0; i <= kSegments; ++i) {
    const double pressure = static_cast<double>(i) / static_cast<double>(kSegments);
    const double shaped = std::pow(pressure, gamma_);
    const double x = left + graph_width * pressure;
    const double y = top + graph_height * (1.0 - shaped);
    if (i == 0) {
      path.moveTo(x, y);
    } else {
      path.lineTo(x, y);
    }
  }
  p.drawPath(path);

  // Control handle
  const double handle_x = left + graph_width * 0.5;
  const double handle_y = top + graph_height * (1.0 - std::pow(0.5, gamma_));

  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawEllipse(QPointF(handle_x, handle_y), 6.0, 6.0);

  p.setBrush(accent);
  p.drawEllipse(QPointF(handle_x, handle_y), 4.2, 4.2);

  p.restore();
}

} // namespace kreart::ui
