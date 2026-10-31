#pragma once

#include <QImage>
#include <QWidget>

#include "kreart/core/tiled_image.h"

namespace kreart::ui::mobile {

// Touch HSV picker: a 280x280 SV square, a 28 px hue ring around it, and a
// 280x24 alpha bar. Mouse-only on purpose (no WA_AcceptTouchEvents) so Qt
// synthesises mouse events from a finger, which keeps the drag logic simple.
class MobileColorPicker : public QWidget {
  Q_OBJECT

public:
  explicit MobileColorPicker(QWidget *parent = nullptr);

  void set_color(core::Rgba8 color, bool emit_signal = false);
  core::Rgba8 color() const;

  QSize sizeHint() const override;

signals:
  void colorChanged(const core::Rgba8 &color);
  void colorCommitted(const core::Rgba8 &color);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  enum class DragTarget { NONE, SV, HUE, ALPHA };

  void update_from_pointer(const QPointF &pos);
  void rebuild_surfaces();
  QRectF sv_rect() const;
  QRectF alpha_rect() const;
  QPointF ring_center() const;
  double ring_outer() const;
  double ring_inner() const;

  double hue_ = 45.0;        // 0..360
  double saturation_ = 0.7;  // 0..1
  double value_ = 0.91;      // 0..1
  int alpha_ = 255;

  DragTarget drag_ = DragTarget::NONE;
  QPointF drag_pos_;
  bool dragging_ = false;

  QImage sv_image_;
  QImage ring_image_;
  double cached_hue_ = -1.0;
};

} // namespace kreart::ui::mobile
