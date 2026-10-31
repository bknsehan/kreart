#pragma once

#include <map>

#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QTouchEvent>

#include "kreart/ui/canvas.h"

namespace kreart::ui {

// Touch-first Canvas subclass used by the Android editor.
//
// The shared Canvas only understands exactly two touch points (pan/pinch) and
// ignores single-finger contact entirely, so finger painting is impossible
// there. MobileCanvas intercepts every touch event before Canvas sees it and
// synthesises the mouse press/move/release sequence that Canvas already handles
// well, while keeping multi-finger gestures for navigation.
//
// Precedence on every touch event:
//   1. stylus seen recently  -> drop everything (palm rejection)
//   2. stroke in progress    -> feed the synthetic stroke, ignore extra fingers
//   3. 3+ points             -> accumulate a tap candidate, forward nothing
//   4. 2 points              -> hold until movement exceeds slop, then pan/pinch
//   5. 1 point               -> synthetic press immediately (paint)
class MobileCanvas : public Canvas {
  Q_OBJECT

public:
  explicit MobileCanvas(core::Document &doc, QWidget *parent = nullptr);
  ~MobileCanvas() override = default;

  // Test seam: pretends a stylus was just seen so palm rejection engages.
  void simulate_stylus_seen();

signals:
  void longPressAt(QPointF local);
  void threeFingerTap();
  void threeFingerDoubleTap();
  void twoFingerTap();
  void twoFingerDoubleTap();

protected:
  bool event(QEvent *event) override;
  void tabletEvent(QTabletEvent *event) override;

private:
  struct TouchTrack {
    int id = 0;
    QPointF start;
    QPointF last;
    QElapsedTimer age;
    bool moved = false;
    bool synthetic_press_sent = false;
  };

  bool handle_touch(QTouchEvent *event);
  void send_synthetic_press(const QPointF &pos);
  void send_synthetic_move(const QPointF &pos);
  void send_synthetic_release(const QPointF &pos);
  void cancel_long_press();
  void on_long_press_fired();
  void reset_touch_state();
  bool point_is_finger(const QEventPoint &point) const;
  bool tool_paints() const;
  bool tool_accepts_single_finger() const;
  void emit_tap_for_finger_count(int peak, const QPointF &where);

  std::map<int, TouchTrack> tracks_;
  int active_fingers_ = 0;
  int peak_fingers_ = 0;
  int stroke_point_id_ = -1;
  bool gesture_mode_ = false;
  bool stroke_from_touch_ = false;
  bool gesture_committed_ = false;
  bool tap_candidate_ = false;
  bool any_point_moved_ = false;
  QElapsedTimer tap_clock_;
  QPointF gesture_origin_;
  QPointF last_synthetic_pos_;
  QTimer long_press_timer_;
  QPointF long_press_pos_;
  bool long_press_fired_ = false;

  QElapsedTimer stylus_seen_recently_;

  // Double-tap recognition for 2- and 3-finger taps.
  QElapsedTimer last_two_finger_tap_;
  QElapsedTimer last_three_finger_tap_;
};

} // namespace kreart::ui
