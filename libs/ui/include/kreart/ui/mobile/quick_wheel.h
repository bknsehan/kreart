#pragma once

#include <QElapsedTimer>
#include <QPointF>
#include <QWidget>

namespace kreart::ui::mobile {

// Radial quick wheel shown on a long press over the canvas. Eight sectors
// replace the desktop QuickMenu (which is a fixed 300x360 dialog unreachable by
// touch). Sectors mirror the desktop quick-menu actions.
class QuickWheel : public QWidget {
  Q_OBJECT

public:
  explicit QuickWheel(QWidget *host);

  void open_at(const QPointF &host_pos);
  void close_wheel();
  bool is_open() const { return is_open_; }

  void set_center_color(const QColor &color);
  void set_eraser_active(bool active);

  static constexpr int kWheelSize = 300;

signals:
  void undoRequested();
  void redoRequested();
  void toggleEraserRequested();
  void zoomFitRequested();
  void mirrorRequested();
  void resetRotationRequested();
  void brushSizeIncreaseRequested();
  void brushSizeDecreaseRequested();
  void colorRequested();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  enum class Sector {
    NONE = -1,
    UNDO = 0,
    REDO = 1,
    ERASER = 2,
    FIT = 3,
    MIRROR = 4,
    RESET_ROTATION = 5,
    SIZE_UP = 6,
    SIZE_DOWN = 7,
  };

  Sector sector_at(const QPointF &pos) const;
  void activate(Sector sector);
  QString sector_label(Sector sector) const;
  QString sector_icon(Sector sector) const;
  QRectF center_disc() const;

  QWidget *host_ = nullptr;
  bool is_open_ = false;
  Sector hover_ = Sector::NONE;
  QPointF center_;
  QColor center_color_{232, 180, 74};
  bool eraser_active_ = false;
};

} // namespace kreart::ui::mobile
