#pragma once

#include <cstdint>
#include <numbers>
#include <vector>
#include <utility>

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTouchEvent>
#include <QKeyEvent>
#include <QTimer>

#include "kreart/brush/round_brush.h"
#include "kreart/core/document.h"
#include "kreart/render/framebuffer.h"
#include "kreart/render/gl_renderer.h"
#include "kreart/core/project_tracker.h"
#include "kreart/io/wal_journal.h"
#include "kreart/io/timelapse.h"

namespace kreart::ui {

class Canvas : public QOpenGLWidget, protected QOpenGLExtraFunctions {
  Q_OBJECT

public:
  enum class Tool {
    BRUSH,
    ERASER,
    SMUDGE,
    RECT_SELECT,
    ELLIPSE_SELECT,
    LASSO_SELECT,
    MAGIC_WAND,
    MOVE,
    COLOR_PICKER,
    FILL,
  };

  enum class LassoMode {
    FREEHAND,
    MAGIC_OBJECT,
  };
  enum class TransformDragMode {
    NONE,
    TRANSLATE,
    ROTATE,
    SCALE
  };

  struct PointerInfo {
    double x = 0.0;
    double y = 0.0;
    double pressure = 0.0;
    double tilt_x = 0.0;
    double tilt_y = 0.0;
    bool has_pressure = false;
    bool has_tilt = false;
  };

  explicit Canvas(core::Document &document, QWidget *parent = nullptr);
  ~Canvas() override;

  void refresh();
  void cancel_stroke();

  // Drops all cached GPU state (layer textures, composite FBO contents) and
  // forces the next paintGL to re-upload and recomposite from scratch. Needed
  // whenever the document is replaced, because layer ids restart at 1 and
  // would otherwise reuse textures holding the previous document's pixels.
  void invalidate_gpu_state();
  // Selection commit/cancel/clear driven by configurable shortcuts. They live
  // on Canvas because move_auto_selected_ (the auto-bbox-select state) is owned
  // here and must be reset atomically with the selection change.
  void commit_selection_key();
  void cancel_selection_key();
  void clear_selection_content_key();

  void set_tool(Tool tool);
  Tool tool() const { return tool_; }
  int wand_tolerance() const { return wand_tolerance_; }
  void set_wand_tolerance(int tol) { wand_tolerance_ = std::clamp(tol, 0, 255); }
  bool wand_contiguous() const { return wand_contiguous_; }
  void set_wand_contiguous(bool cont) { wand_contiguous_ = cont; }

  void set_tilt_enabled(bool enabled);
  bool tilt_enabled() const { return brush_.settings().tilt_enabled; }

  bool erasing() const { return effective_eraser(); }

  void set_brush_size(double size);
  double brush_size() const { return brush_.settings().size; }

  void set_brush_opacity(double opacity);
  void set_brush_color(core::Rgba8 color);

  void set_pressure_curve(double gamma);
  double pressure_curve() const { return brush_.settings().pressure_curve; }

  void set_brush_settings(const brush::Settings &settings);
  const brush::Settings &brush_settings() const { return brush_.settings(); }

  void set_brush_color_rate(double rate);
  double brush_color_rate() const { return brush_.settings().color_rate; }

  void set_brush_smudge_rate(double rate);
  double brush_smudge_rate() const { return brush_.settings().smudge_rate; }

  bool texture_enabled() const { return brush_.settings().texture_enabled; }
  void set_texture_enabled(bool enabled);

  double texture_scale() const { return brush_.settings().texture_scale; }
  void set_texture_scale(double scale);

  double texture_strength() const { return brush_.settings().texture_strength; }
  void set_texture_strength(double strength);

  void set_texture_pattern(brush::TextureMaskPtr pattern);
  brush::TextureMaskPtr texture_pattern() const { return brush_.settings().texture; }

  void set_smoothing_mode(brush::SmoothingMode mode);
  brush::SmoothingMode smoothing_mode() const { return brush_.settings().smoothing_mode; }

  void set_brush_smoothing(double smoothing);
  double brush_smoothing() const { return brush_.settings().smoothing; }

  void set_pixelated(bool pixelated);
  bool pixelated() const { return pixelated_; }

  void set_show_grid(bool show);
  bool show_grid() const { return show_grid_; }

  void set_grid_size(int size);
  int grid_size() const { return grid_size_; }

  void set_grid_color(core::Rgba8 color);
  core::Rgba8 grid_color() const { return grid_color_; }

  void zoom_in();
  void zoom_out();
  void zoom_fit();
  double zoom_percent() const { return view_scale_ * 100.0; }
  double pan_x() const { return pan_x_; }
  double pan_y() const { return pan_y_; }
  void pan_by(double dx, double dy);
  void set_rotation(double radians);
  double rotation() const { return view_rotation_; }
  double rotation_degrees() const { return view_rotation_ * 180.0 / std::numbers::pi; }
  void rotate_by(double delta_radians);
  void reset_rotation();

  void map_to_widget(double doc_x, double doc_y, double &widget_x, double &widget_y) const;
  bool map_to_document(double widget_x, double widget_y, brush::Point &point);
  bool map_to_document_clamped(double widget_x, double widget_y, brush::Point &point);

  void toggle_mirror_horizontal();
  void toggle_mirror_vertical();
  bool mirror_horizontal() const { return mirror_horizontal_; }
  bool mirror_vertical() const { return mirror_vertical_; }
  void set_mirror_horizontal(bool mirror);
  void set_mirror_vertical(bool mirror);

  void set_lasso_mode(LassoMode mode) { lasso_mode_ = mode; }
  LassoMode lasso_mode() const { return lasso_mode_; }
  void set_selection_layer_mode(bool active);
  bool selection_layer_mode() const { return selection_layer_mode_; }
  void sync_selection_image_from_mask();
  void sync_selection_mask_from_image();
  core::TiledImage &selection_image() { return selection_image_; }
  const core::TiledImage &selection_image() const { return selection_image_; }
  TransformDragMode hit_test_transform(double wx, double wy);
  void update_move_tool_cursor(double wx, double wy);
  bool move_auto_selected() const { return move_auto_selected_; }
  void draw_overlays(QPainter &painter);
  void set_symmetry_vertical(bool enabled);
  bool symmetry_vertical() const { return symmetry_vertical_; }
  void toggle_symmetry_vertical();

  void set_symmetry_horizontal(bool enabled);
  bool symmetry_horizontal() const { return symmetry_horizontal_; }
  void toggle_symmetry_horizontal();

  void set_symmetry_axis_x(double x);
  double symmetry_axis_x() const { return symmetry_axis_x_; }
  void set_symmetry_axis_y(double y);
  double symmetry_axis_y() const { return symmetry_axis_y_; }
  void reposition_symmetry_axes();

  double cursor_widget_x() const { return cursor_widget_valid_ ? cursor_widget_x_ : width() * 0.5; }
  double cursor_widget_y() const { return cursor_widget_valid_ ? cursor_widget_y_ : height() * 0.5; }

  bool is_dark_theme() const { return is_dark_theme_; }
  void set_dark_theme(bool dark);

  bool gl_enabled() const { return gl_enabled_; }
  void set_gl_enabled(bool enabled);
  bool stroke_active() const { return stroke_active_; }

  // Mobile opt-ins. All default off so desktop behaviour is unchanged.
  //
  // Records each brush stroke as a single undo transaction. Without this the
  // desktop brush path never calls begin_change(), so strokes are absent from
  // undo history and cancel_stroke() has no snapshot to roll back to.
  void set_stroke_history_enabled(bool enabled) { stroke_history_enabled_ = enabled; }
  bool stroke_history_enabled() const { return stroke_history_enabled_; }

  // Renders the GL viewport in device pixels. QOpenGLWidget sizes its FBO in
  // device pixels while paintGL currently passes logical width()/height(), so
  // on a dpr-2 screen the canvas would draw into one quarter of the surface.
  void set_use_device_pixels(bool enabled) { use_device_pixels_ = enabled; }
  bool use_device_pixels() const { return use_device_pixels_; }
  core::ProjectTracker &tracker() { return tracker_; }
  const core::ProjectTracker &tracker() const { return tracker_; }
  io::WalJournal &wal_journal() { return wal_journal_; }
  const io::WalJournal &wal_journal() const { return wal_journal_; }
  io::TimelapseRecorder &timelapse_recorder() { return timelapse_recorder_; }
  const io::TimelapseRecorder &timelapse_recorder() const { return timelapse_recorder_; }
  void capture_timelapse_frame(bool force = false);

  // Combine mode applied by touch selections (no keyboard modifiers on a
  // phone). Keyboard modifiers still win when present, so desktop is unchanged.
  void set_selection_combine_mode(core::SelectionCombineMode mode) { selection_combine_mode_ = mode; }
  core::SelectionCombineMode selection_combine_mode() const { return selection_combine_mode_; }

signals:
  void rotationChanged(double degrees);
  void cursorMoved(const PointerInfo &info);
  void documentChanged();
  void eraserModeChanged(bool erasing);
  void zoomChanged(double percent);
  void colorPicked(const core::Rgba8 &color);
  void quickMenuRequested(double x, double y);
  void activeLayerAutoSelected(std::size_t index);

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  bool event(QEvent *event) override;
  void tabletEvent(QTabletEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  void handleTouchEvent(QTouchEvent *event);
  struct InputSample {
    double pressure = 1.0;
    double tilt_x = 0.0;
    double tilt_y = 0.0;
    double rotation = 0.0;
    bool has_pressure = false;
    bool has_tilt = false;
    bool has_rotation = false;
    bool is_eraser = false;
  };
  struct PointerEvent {
    QPointF pos;
    InputSample sample;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
  };

  void handle_pointer_press(const PointerEvent &ev);
  void handle_pointer_move(const PointerEvent &ev);
  void handle_pointer_release(const PointerEvent &ev);
  void handle_pen_gesture_move(const PointerEvent &ev);

  enum class PenGestureMode { NONE, ZOOM, ROTATE };
  bool pen_gesture_active_ = false;
  PenGestureMode pen_gesture_mode_ = PenGestureMode::NONE;
  QPointF pen_gesture_start_;
  QPointF pen_gesture_last_;
  double pen_gesture_initial_twist_ = 0.0;
  bool pen_gesture_has_twist_ = false;


  void update_transform(int width, int height);
  void zoom_at(double factor, double widget_x, double widget_y);
  void start_pan(double widget_x, double widget_y);
  void stop_pan();

  void start_rotate(double x, double y);
  void update_rotate(double x, double y);
  void stop_rotate();

  void remember_cursor(double widget_x, double widget_y);
  void emit_cursor(double widget_x, double widget_y, const InputSample &input, bool contact);

  bool effective_eraser() const;
  void update_brush_mode();
  void finish_stroke();

  void expand_stroke_bbox(const std::vector<brush::BrushDab> &dabs);
  core::Document &document_;
  brush::RoundBrush brush_;
  render::CanvasFramebuffer framebuffer_;
  render::OpenGLRenderer gl_renderer_;

  double view_scale_ = 1.0;
  double view_x_ = 0.0;
  double view_y_ = 0.0;
  double fit_scale_ = 1.0;
  double zoom_ = 1.0;
  double pan_x_ = 0.0;
  double pan_y_ = 0.0;
  double last_notified_zoom_ = -1.0;

  double cursor_widget_x_ = 0.0;
  double cursor_widget_y_ = 0.0;
  bool cursor_widget_valid_ = false;

  bool space_held_ = false;
  bool shift_held_ = false;
  bool panning_ = false;
  double pan_last_x_ = 0.0;
  double pan_last_y_ = 0.0;

  double view_rotation_ = 0.0;
  double pivot_x_ = 0.0;
  double pivot_y_ = 0.0;
  bool rotate_active_ = false;
  double rotate_start_x_ = 0.0;
  double rotate_start_y_ = 0.0;
  double rotate_start_angle_ = 0.0;

  double cursor_x_ = 0.0;
  double cursor_y_ = 0.0;
  double cursor_tilt_x_ = 0.0;
  double cursor_tilt_y_ = 0.0;
  bool cursor_inside_ = false;

  bool stroke_active_ = false;
  bool eraser_tip_active_ = false;
  bool barrel_eraser_ = false;
  bool eraser_mode_ = false;
  brush::Point last_stroke_input_;
  Tool tool_ = Tool::BRUSH;
  int wand_tolerance_ = 32;
  bool wand_contiguous_ = true;
  int stroke_min_x_ = 0;
  int stroke_min_y_ = 0;
  int stroke_max_x_ = 0;
  int stroke_max_y_ = 0;
  bool stroke_gpu_active_ = false;
  std::size_t stroke_layer_id_ = 0;
  brush::Mode stroke_mode_ = brush::Mode::PAINT;

  bool pixelated_ = false;
  bool show_grid_ = false;
  int grid_size_ = 32;
  core::Rgba8 grid_color_{128, 128, 140, 90};
  bool mirror_horizontal_ = false;
  int last_doc_width_ = 0;
  int last_doc_height_ = 0;
  bool mirror_vertical_ = false;

  bool selecting_ = false;
  brush::Point select_start_;
  brush::Point select_current_;
  std::vector<std::pair<double, double>> current_lasso_points_;
  double march_offset_ = 0.0;
  QTimer march_timer_;

  bool moving_ = false;
  brush::Point move_start_;
  bool move_auto_selected_ = false;
  brush::Point move_current_;
  TransformDragMode transform_drag_mode_ = TransformDragMode::NONE;
  int initial_offset_x_ = 0;
  int initial_offset_y_ = 0;
  double initial_rotation_ = 0.0;
  double initial_scale_x_ = 1.0;
  double initial_scale_y_ = 1.0;

  bool selection_layer_mode_ = false;
  core::TiledImage selection_image_;
  core::Rgba8 user_brush_color_{0, 0, 0, 255};
  LassoMode lasso_mode_ = LassoMode::FREEHAND;
  bool symmetry_vertical_ = false;
  bool symmetry_horizontal_ = false;
  double symmetry_axis_x_ = 0.0;
  double symmetry_axis_y_ = 0.0;

  bool gl_enabled_ = true;
  bool is_dark_theme_ = true;
  bool stroke_history_enabled_ = false;
  bool use_device_pixels_ = false;
  std::uint64_t last_composite_revision_ = 0;
  bool force_composite_ = true;
  core::SelectionCombineMode selection_combine_mode_ = core::SelectionCombineMode::REPLACE;

  // Touch tracking
  bool touch_active_ = false;
  QPointF last_touch_center_;
  double last_touch_dist_ = 0.0;
  double initial_touch_angle_ = 0.0;
  double touch_rotation_start_ = 0.0;
  bool touch_rotation_engaged_ = false;
  // Stroke tracking, recovery journal, and timelapse
  core::ProjectTracker tracker_;
  io::WalJournal wal_journal_;
  io::TimelapseRecorder timelapse_recorder_;
  std::uint64_t last_timelapse_capture_ms_ = 0;
  std::uint64_t stroke_start_time_ms_ = 0;
};

} // namespace kreart::ui
