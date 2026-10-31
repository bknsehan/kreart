#pragma once

#include <QWidget>

namespace kreart::ui {

// A small interactive graph of the brush pressure response. The horizontal
// axis is incoming pressure and the vertical axis is the shaped response. One
// control point at 50% pressure sets the gamma, which keeps the curve monotonic
// and makes the mapping easy to reason about while painting.
class PressureCurve : public QWidget {
  Q_OBJECT

public:
  explicit PressureCurve(QWidget *parent = nullptr);

  void set_gamma(double gamma);
  double gamma() const {
    return gamma_;
  }
  void reset();

signals:
  void curveChanged(double gamma);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  QSize sizeHint() const override;

private:
  void set_gamma_from_pointer(double widget_y);

  double gamma_ = 1.0;
  bool dragging_ = false;
};

} // namespace kreart::ui
