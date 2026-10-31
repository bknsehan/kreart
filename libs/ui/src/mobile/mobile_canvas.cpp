#include "kreart/ui/mobile/mobile_canvas.h"

#include <QApplication>
#include <QLineF>
#include <QMouseEvent>

#include <algorithm>

namespace kreart::ui {
namespace {

// All gesture thresholds live here so they are easy to tune on hardware.
constexpr int kLongPressMs = 500;
constexpr int kTapMaxMs = 280;
constexpr int kTwoFingerTapMaxMs = 260;
constexpr int kDoubleTapMs = 400;
constexpr double kTapSlopPx = 16.0;
constexpr double kLongPressSlopPx = 12.0;
constexpr int kPalmRejectMs = 1500;

// QEventPoint(id, state, scenePos, globalPos) leaves position() at (0,0), which
// is what synthetic test points look like. Real platform touch points always
// carry a widget-local position, so fall back to the scene position only when
// the local one is unset.
QPointF point_position(const QEventPoint &p) {
  const QPointF local = p.position();
  if (local.isNull()) {
    const QPointF scene = p.scenePosition();
    if (!scene.isNull()) return scene;
  }
  return local;
}

} // namespace

MobileCanvas::MobileCanvas(core::Document &doc, QWidget *parent)
    : Canvas(doc, parent) {
  set_stroke_history_enabled(true);
  set_use_device_pixels(true);
  setAttribute(Qt::WA_AcceptTouchEvents, true);
  setAttribute(Qt::WA_Hover, false);

  long_press_timer_.setSingleShot(true);
  long_press_timer_.setInterval(kLongPressMs);
  connect(&long_press_timer_, &QTimer::timeout, this, &MobileCanvas::on_long_press_fired);
}

void MobileCanvas::simulate_stylus_seen() {
  stylus_seen_recently_.restart();
}

bool MobileCanvas::point_is_finger(const QEventPoint &point) const {
  const QPointingDevice *device = point.device();
  if (!device) return true;  // synthetic test points carry no device
  return device->type() == QInputDevice::DeviceType::TouchScreen;
}

bool MobileCanvas::tool_paints() const {
  switch (tool()) {
    case Tool::BRUSH:
    case Tool::ERASER:
    case Tool::SMUDGE:
    case Tool::FILL:
      return true;
    default:
      return false;
  }
}

bool MobileCanvas::tool_accepts_single_finger() const {
  // Every tool is usable with one finger: paint tools paint, selection tools
  // select, MOVE transforms and COLOR_PICKER samples.
  return true;
}

bool MobileCanvas::event(QEvent *e) {
  switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
      handle_touch(static_cast<QTouchEvent *>(e));
      e->accept();
      return true;
    default:
      break;
  }
  return Canvas::event(e);
}

void MobileCanvas::tabletEvent(QTabletEvent *event) {
  stylus_seen_recently_.restart();
  Canvas::tabletEvent(event);
}

void MobileCanvas::send_synthetic_press(const QPointF &pos) {
  last_synthetic_pos_ = pos;
  QMouseEvent press(QEvent::MouseButtonPress, pos, QPointF(mapToGlobal(pos.toPoint())),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(this, &press);
}

void MobileCanvas::send_synthetic_move(const QPointF &pos) {
  last_synthetic_pos_ = pos;
  QMouseEvent move(QEvent::MouseMove, pos, QPointF(mapToGlobal(pos.toPoint())),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(this, &move);
}

void MobileCanvas::send_synthetic_release(const QPointF &pos) {
  last_synthetic_pos_ = pos;
  QMouseEvent release(QEvent::MouseButtonRelease, pos, QPointF(mapToGlobal(pos.toPoint())),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(this, &release);
}

void MobileCanvas::cancel_long_press() {
  if (long_press_timer_.isActive()) {
    long_press_timer_.stop();
  }
}

void MobileCanvas::on_long_press_fired() {
  long_press_fired_ = true;
  // cancel_stroke() is a genuine rollback because stroke history is enabled, so
  // a long press never leaves paint behind.
  cancel_stroke();
  send_synthetic_release(last_synthetic_pos_);
  stroke_from_touch_ = false;
  stroke_point_id_ = -1;
  // Whatever fingers remain must not start a fresh stroke.
  gesture_mode_ = true;
  gesture_committed_ = true;
  emit longPressAt(long_press_pos_);
}

void MobileCanvas::reset_touch_state() {
  tracks_.clear();
  active_fingers_ = 0;
  peak_fingers_ = 0;
  gesture_mode_ = false;
  stroke_from_touch_ = false;
  stroke_point_id_ = -1;
  gesture_committed_ = false;
  tap_candidate_ = false;
  any_point_moved_ = false;
  long_press_fired_ = false;
  cancel_long_press();
}

void MobileCanvas::emit_tap_for_finger_count(int peak, const QPointF &where) {
  (void)where;
  if (peak >= 3) {
    if (last_three_finger_tap_.isValid() && last_three_finger_tap_.elapsed() < kDoubleTapMs) {
      last_three_finger_tap_.invalidate();
      emit threeFingerDoubleTap();
    } else {
      last_three_finger_tap_.restart();
      emit threeFingerTap();
    }
    return;
  }
  if (peak == 2) {
    if (last_two_finger_tap_.isValid() && last_two_finger_tap_.elapsed() < kDoubleTapMs) {
      last_two_finger_tap_.invalidate();
      emit twoFingerDoubleTap();
    } else {
      last_two_finger_tap_.restart();
      emit twoFingerTap();
    }
  }
}

bool MobileCanvas::handle_touch(QTouchEvent *event) {
  const QEvent::Type type = event->type();

  // 1. Palm rejection: a stylus was used very recently, so every touch contact
  //    is a palm. Consume without painting or gesturing.
  if (stylus_seen_recently_.isValid() && stylus_seen_recently_.elapsed() < kPalmRejectMs) {
    if (stroke_from_touch_) {
      // Should not happen (touch is dropped), but never leave a dangling stroke.
      cancel_stroke();
      stroke_from_touch_ = false;
    }
    reset_touch_state();
    return true;
  }

  if (type == QEvent::TouchCancel) {
    cancel_long_press();
    if (stroke_from_touch_) {
      send_synthetic_release(last_synthetic_pos_);
    }
    reset_touch_state();
    return true;
  }

  const auto &points = event->points();
  const bool had_tracks = !tracks_.empty();

  // Refresh the track table from the points present in this event.
  for (const auto &p : points) {
    if (!point_is_finger(p)) continue;
    if (p.state() == QEventPoint::Released) continue;

    const QPointF pos = point_position(p);
    auto it = tracks_.find(p.id());
    if (it == tracks_.end()) {
      TouchTrack track;
      track.id = p.id();
      track.start = pos;
      track.last = pos;
      track.age.start();
      tracks_.emplace(p.id(), track);
    } else {
      it->second.last = pos;
      const double travelled = QLineF(it->second.start, pos).length();
      if (travelled > kTapSlopPx) {
        it->second.moved = true;
        any_point_moved_ = true;
      }
      if (travelled > kLongPressSlopPx) {
        cancel_long_press();
      }
    }
  }

  active_fingers_ = static_cast<int>(tracks_.size());
  peak_fingers_ = std::max(peak_fingers_, active_fingers_);
  if (!had_tracks && active_fingers_ > 0) {
    tap_clock_.start();
    tap_candidate_ = true;
  }

  // 2. A synthetic stroke is already running: feed it, ignore extra fingers.
  if (stroke_from_touch_) {
    if (type == QEvent::TouchUpdate) {
      auto it = tracks_.find(stroke_point_id_);
      if (it != tracks_.end()) {
        send_synthetic_move(it->second.last);
      }
    } else if (type == QEvent::TouchEnd) {
      bool stroke_finger_released = false;
      for (const auto &p : points) {
        if (p.state() != QEventPoint::Released) continue;
        if (p.id() == stroke_point_id_) stroke_finger_released = true;
        tracks_.erase(p.id());
      }
      if (stroke_finger_released) {
        send_synthetic_release(last_synthetic_pos_);
        stroke_from_touch_ = false;
        stroke_point_id_ = -1;
        if (!tracks_.empty()) {
          // Remaining fingers must not turn into a paint stroke.
          gesture_mode_ = true;
          gesture_committed_ = true;
        }
      }
      active_fingers_ = static_cast<int>(tracks_.size());
      if (tracks_.empty()) reset_touch_state();
    }
    return true;
  }

  // 3. Committed multi-finger gesture: forward to Canvas' pan/pinch/rotate.
  if (gesture_mode_ && gesture_committed_) {
    if (type == QEvent::TouchEnd || type == QEvent::TouchUpdate) {
      Canvas::event(event);
    }
    if (type == QEvent::TouchEnd) {
      for (const auto &p : points) {
        if (p.state() == QEventPoint::Released) tracks_.erase(p.id());
      }
      active_fingers_ = static_cast<int>(tracks_.size());
      if (active_fingers_ == 0) {
        const bool was_tap = !gesture_committed_ && !any_point_moved_ &&
                             peak_fingers_ >= 2 && tap_clock_.isValid() &&
                             tap_clock_.elapsed() <= kTapMaxMs;
        const int peak = peak_fingers_;
        reset_touch_state();
        if (was_tap) emit_tap_for_finger_count(peak, QPointF());
      }
    }
    return true;
  }

  // 4. Three or more points: accumulate a tap candidate, forward nothing.
  if (active_fingers_ >= 3) {
    if (type == QEvent::TouchEnd) {
      for (const auto &p : points) {
        if (p.state() == QEventPoint::Released) tracks_.erase(p.id());
      }
      active_fingers_ = static_cast<int>(tracks_.size());
      if (active_fingers_ == 0) {
        const bool was_tap = !any_point_moved_ && peak_fingers_ >= 3 &&
                             tap_clock_.isValid() && tap_clock_.elapsed() <= kTapMaxMs;
        const int peak = peak_fingers_;
        reset_touch_state();
        if (was_tap) emit_tap_for_finger_count(peak, QPointF());
      }
    }
    return true;
  }

  // 5. Two points: enter gesture mode. Canvas is told about the begin so its
  //    pan/pinch baseline is the resting distance, but updates are withheld
  //    until movement exceeds the tap slop so a two-finger tap still works.
  if (active_fingers_ >= 2) {
    if (type == QEvent::TouchBegin) {
      // Establish Canvas' pan/pinch baseline without committing to a gesture, so
      // a two-finger tap can still be recognised.
      gesture_mode_ = true;
      gesture_committed_ = false;
      Canvas::event(event);
    } else if (type == QEvent::TouchUpdate) {
      if (any_point_moved_) {
        gesture_committed_ = true;
        Canvas::event(event);
      }
    } else if (type == QEvent::TouchEnd) {
      for (const auto &p : points) {
        if (p.state() == QEventPoint::Released) tracks_.erase(p.id());
      }
      active_fingers_ = static_cast<int>(tracks_.size());
      Canvas::event(event);
      if (active_fingers_ == 0) {
        const bool was_tap = !gesture_committed_ && !any_point_moved_ &&
                             peak_fingers_ >= 2 && tap_clock_.isValid() &&
                             tap_clock_.elapsed() <= kTwoFingerTapMaxMs;
        const int peak = peak_fingers_;
        reset_touch_state();
        if (was_tap) emit_tap_for_finger_count(peak, QPointF());
      }
    }
    return true;
  }

  // 6. One point: paint immediately with a synthetic mouse press.
  if (active_fingers_ == 1) {
    const auto &track = tracks_.begin()->second;
    if (type == QEvent::TouchBegin) {
      if (tool_accepts_single_finger()) {
        stroke_point_id_ = track.id;
        long_press_pos_ = track.last;
        long_press_fired_ = false;
        stroke_from_touch_ = true;
        send_synthetic_press(track.last);
        long_press_timer_.start();
      }
    } else if (type == QEvent::TouchUpdate) {
      send_synthetic_move(track.last);
    } else if (type == QEvent::TouchEnd) {
      cancel_long_press();
      send_synthetic_release(track.last);
      reset_touch_state();
    }
    return true;
  }

  if (type == QEvent::TouchEnd) {
    reset_touch_state();
  }
  return true;
}

} // namespace kreart::ui
