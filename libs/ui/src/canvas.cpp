#include "kreart/ui/canvas.h"
#include "kreart/ui/app_settings.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace kreart::ui {
namespace {

constexpr double kCanvasMargin = 32.0;
constexpr double kMinVisibleDocument = 64.0;
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 16.0;
constexpr double kZoomStep = 1.25;
constexpr double kZoomWheelStep = 1.15;
constexpr double kPanWheelStep = 48.0;

} // namespace

Canvas::Canvas(core::Document &document, QWidget *parent)
    : QOpenGLWidget(parent), document_(document),
      framebuffer_(document.width(), document.height()),
      selection_image_(document.width(), document.height()) {
  setMouseTracking(true);
  setAttribute(Qt::WA_AcceptTouchEvents, true);
  setCursor(Qt::CrossCursor);
  setFocusPolicy(Qt::StrongFocus);

  update_brush_mode();
  symmetry_axis_x_ = document_.width() * 0.5;
  symmetry_axis_y_ = document_.height() * 0.5;
  last_doc_width_ = document_.width();
  last_doc_height_ = document_.height();

  auto initial_settings = brush_.settings();
  initial_settings.symmetry_axis_x = symmetry_axis_x_;
  initial_settings.symmetry_axis_y = symmetry_axis_y_;
  brush_.set_settings(initial_settings);
  brush_.set_selection(&document_.selection());

  connect(&march_timer_, &QTimer::timeout, this, [this]() {
    if (document_.selection().active() || selecting_) {
      march_offset_ += 1.0;
      if (march_offset_ >= 16.0) march_offset_ = 0.0;
      update();
    }
  });
  march_timer_.start(80);
  connect(&AppSettings::instance(), &AppSettings::transparencySettingsChanged, this, [this]() {
    update();
  });
  tracker_.reset(document_.width(), document_.height());
  wal_journal_.open(io::WalJournal::default_journal_path_for({}, tracker_.session_uuid()),
                    document_.width(), document_.height(), tracker_.session_uuid());
  timelapse_recorder_.init(tracker_.session_uuid());
}

Canvas::~Canvas() {
  wal_journal_.close();
  timelapse_recorder_.stop();
  if (isValid()) {
    makeCurrent();
    gl_renderer_.shutdown();
    doneCurrent();
  }
}

void Canvas::initializeGL() {
  if (!context() || !context()->isValid()) {
    gl_enabled_ = false;
    return;
  }
  initializeOpenGLFunctions();
  gl_renderer_.shutdown();
  gl_renderer_.init();
  force_composite_ = true;
}

void Canvas::resizeGL(int w, int h) {
  update_transform(w, h);
}

void Canvas::refresh() {
  if (last_doc_width_ != document_.width() || last_doc_height_ != document_.height()) {
    last_doc_width_ = document_.width();
    last_doc_height_ = document_.height();
    reposition_symmetry_axes();
  }
  document_.mark_composite_dirty();
  if (!gl_enabled_) framebuffer_.mark_all_dirty();
  update();
}

void Canvas::invalidate_gpu_state() {
  gl_renderer_.mark_all_dirty();
  force_composite_ = true;
  update();
}

void Canvas::reposition_symmetry_axes() {
  set_symmetry_axis_x(document_.width() * 0.5);
  set_symmetry_axis_y(document_.height() * 0.5);
}

void Canvas::cancel_stroke() {
  if (stroke_active_) {
    if (stroke_gpu_active_) {
      makeCurrent();
      brush_.end();
      gl_renderer_.clear_stroke();
      doneCurrent();
      stroke_gpu_active_ = false;
    } else {
      brush_.end();
    }
    stroke_active_ = false;
    document_.cancel_change();
    refresh();
  }
}

void Canvas::expand_stroke_bbox(const std::vector<brush::BrushDab> &dabs) {
  for (const auto &dab : dabs) {
    const float bound = std::max(dab.major_radius, dab.minor_radius) + 2.0f;
    const int min_x = static_cast<int>(std::floor(dab.x - bound));
    const int min_y = static_cast<int>(std::floor(dab.y - bound));
    const int max_x = static_cast<int>(std::ceil(dab.x + bound));
    const int max_y = static_cast<int>(std::ceil(dab.y + bound));
    stroke_min_x_ = std::min(stroke_min_x_, min_x);
    stroke_min_y_ = std::min(stroke_min_y_, min_y);
    stroke_max_x_ = std::max(stroke_max_x_, max_x);
    stroke_max_y_ = std::max(stroke_max_y_, max_y);
  }
}

void Canvas::set_tool(Tool tool) {
  if (tool_ != tool) {
    if (stroke_active_) finish_stroke();
    if (tool_ == Tool::MOVE) {
      if (document_.has_floating_selection()) {
        document_.commit_move_selection();
      }
      if (move_auto_selected_) {
        document_.selection().clear();
        move_auto_selected_ = false;
      }
    }
    tool_ = tool;
    update_brush_mode();
    if (tool_ == Tool::MOVE) {
      setCursor(Qt::ArrowCursor);
    } else {
      setCursor(Qt::CrossCursor);
    }
    update();
  }
}

void Canvas::set_tilt_enabled(bool enabled) {
  auto s = brush_.settings();
  s.tilt_enabled = enabled;
  brush_.set_settings(s);
}

void Canvas::set_brush_size(double size) {
  auto s = brush_.settings();
  s.size = std::clamp(size, 1.0, 500.0);
  brush_.set_settings(s);
  update();
}

void Canvas::set_brush_opacity(double opacity) {
  auto s = brush_.settings();
  s.opacity = std::clamp(opacity, 0.01, 1.0);
  brush_.set_settings(s);
}

void Canvas::set_brush_color(core::Rgba8 color) {
  auto s = brush_.settings();
  s.color = color;
  brush_.set_settings(s);
}

void Canvas::set_pressure_curve(double gamma) {
  auto s = brush_.settings();
  s.pressure_curve = std::clamp(gamma, 0.15, 6.0);
  brush_.set_settings(s);
  update();
}

void Canvas::set_brush_settings(const brush::Settings &settings) {
  brush::Settings updated = settings;
  updated.size = std::clamp(updated.size, 1.0, 500.0);
  updated.opacity = std::clamp(updated.opacity, 0.01, 1.0);
  updated.hardness = std::clamp(updated.hardness, 0.0, 1.0);
  updated.spacing = std::clamp(updated.spacing, 0.01, 2.0);
  updated.pressure_curve = std::clamp(updated.pressure_curve, 0.15, 6.0);
  brush_.set_settings(updated);
  update_brush_mode();
  update();
}

void Canvas::set_brush_color_rate(double rate) {
  auto s = brush_.settings();
  s.color_rate = std::clamp(rate, 0.0, 1.0);
  brush_.set_settings(s);
}

void Canvas::set_brush_smudge_rate(double rate) {
  auto s = brush_.settings();
  s.smudge_rate = std::clamp(rate, 0.0, 1.0);
  brush_.set_settings(s);
}

void Canvas::set_texture_enabled(bool enabled) {
  auto s = brush_.settings();
  s.texture_enabled = enabled;
  if (enabled && !s.texture) {
    s.texture = brush::TextureMask::create_rough_paper(128);
  }
  brush_.set_settings(s);
}

void Canvas::set_texture_scale(double scale) {
  auto s = brush_.settings();
  s.texture_scale = std::clamp(scale, 0.05, 5.0);
  brush_.set_settings(s);
}

void Canvas::set_texture_strength(double strength) {
  auto s = brush_.settings();
  s.texture_strength = std::clamp(strength, 0.0, 1.0);
  brush_.set_settings(s);
}

void Canvas::set_texture_pattern(brush::TextureMaskPtr pattern) {
  auto s = brush_.settings();
  s.texture = std::move(pattern);
  brush_.set_settings(s);
}

void Canvas::set_smoothing_mode(brush::SmoothingMode mode) {
  auto s = brush_.settings();
  s.smoothing_mode = mode;
  brush_.set_settings(s);
}

void Canvas::set_brush_smoothing(double smoothing) {
  auto s = brush_.settings();
  s.smoothing = std::clamp(smoothing, 0.0, 1.0);
  brush_.set_settings(s);
}

void Canvas::set_pixelated(bool pixelated) {
  if (pixelated_ != pixelated) {
    pixelated_ = pixelated;
    update();
  }
}

void Canvas::set_show_grid(bool show) {
  if (show_grid_ != show) {
    show_grid_ = show;
    update();
  }
}

void Canvas::set_grid_size(int size) {
  const int clamped = std::clamp(size, 2, 512);
  if (grid_size_ != clamped) {
    grid_size_ = clamped;
    if (show_grid_) update();
  }
}

void Canvas::set_grid_color(core::Rgba8 color) {
  grid_color_ = color;
  if (show_grid_) update();
}

void Canvas::zoom_in() {
  if (stroke_active_) return;
  const double x = cursor_widget_valid_ ? cursor_widget_x_ : width() * 0.5;
  const double y = cursor_widget_valid_ ? cursor_widget_y_ : height() * 0.5;
  zoom_at(kZoomStep, x, y);
}

void Canvas::zoom_out() {
  if (stroke_active_) return;
  const double x = cursor_widget_valid_ ? cursor_widget_x_ : width() * 0.5;
  const double y = cursor_widget_valid_ ? cursor_widget_y_ : height() * 0.5;
  zoom_at(1.0 / kZoomStep, x, y);
}

void Canvas::zoom_fit() {
  if (stroke_active_) return;
  zoom_ = 1.0;
  pan_x_ = 0.0;
  pan_y_ = 0.0;
  view_rotation_ = 0.0;
  emit rotationChanged(0.0);
  update_transform(width(), height());
  update();
}

void Canvas::set_rotation(double radians) {
  while (radians > std::numbers::pi) radians -= 2.0 * std::numbers::pi;
  while (radians < -std::numbers::pi) radians += 2.0 * std::numbers::pi;
  if (std::abs(view_rotation_ - radians) > 1e-5) {
    view_rotation_ = radians;
    update_transform(width(), height());
    emit rotationChanged(rotation_degrees());
    update();
  }
}

double Canvas::rotation_degrees() const {
  return view_rotation_ * 180.0 / std::numbers::pi;
}

void Canvas::rotate_by(double delta_radians) {
  set_rotation(view_rotation_ + delta_radians);
}

void Canvas::reset_rotation() {
  set_rotation(0.0);
}

void Canvas::map_to_widget(double doc_x, double doc_y, double &widget_x, double &widget_y) const {
  const double mult_x = mirror_horizontal_ ? -1.0 : 1.0;
  const double mult_y = mirror_vertical_ ? -1.0 : 1.0;
  const double sx = (doc_x - document_.width() * 0.5) * (view_scale_ * mult_x);
  const double sy = (doc_y - document_.height() * 0.5) * (view_scale_ * mult_y);
  const double cos_r = std::cos(view_rotation_);
  const double sin_r = std::sin(view_rotation_);
  widget_x = pivot_x_ + (sx * cos_r - sy * sin_r);
  widget_y = pivot_y_ + (sx * sin_r + sy * cos_r);
}

bool Canvas::map_to_document(double widget_x, double widget_y, brush::Point &point) {
  update_transform(width(), height());
  const double dx = widget_x - pivot_x_;
  const double dy = widget_y - pivot_y_;
  const double cos_r = std::cos(-view_rotation_);
  const double sin_r = std::sin(-view_rotation_);
  const double mult_x = mirror_horizontal_ ? -1.0 : 1.0;
  const double mult_y = mirror_vertical_ ? -1.0 : 1.0;
  const double rx = (dx * cos_r - dy * sin_r) / (view_scale_ * mult_x);
  const double ry = (dx * sin_r + dy * cos_r) / (view_scale_ * mult_y);

  point.x = rx + document_.width() * 0.5;
  point.y = ry + document_.height() * 0.5;
  return point.x >= 0.0 && point.y >= 0.0 && point.x < document_.width() &&
         point.y < document_.height();
}

bool Canvas::map_to_document_clamped(double widget_x, double widget_y, brush::Point &point) {
  const bool inside = map_to_document(widget_x, widget_y, point);
  point.x = std::clamp(point.x, 0.0, static_cast<double>(document_.width()));
  point.y = std::clamp(point.y, 0.0, static_cast<double>(document_.height()));
  return inside;
}

void Canvas::toggle_mirror_horizontal() {
  mirror_horizontal_ = !mirror_horizontal_;
  update();
}

void Canvas::toggle_mirror_vertical() {
  mirror_vertical_ = !mirror_vertical_;
  update();
}

void Canvas::set_mirror_horizontal(bool mirror) {
  if (mirror_horizontal_ != mirror) {
    mirror_horizontal_ = mirror;
    update();
  }
}

void Canvas::set_mirror_vertical(bool mirror) {
  if (mirror_vertical_ != mirror) {
    mirror_vertical_ = mirror;
    update();
  }
}

void Canvas::set_symmetry_vertical(bool enabled) {
  if (symmetry_vertical_ != enabled) {
    symmetry_vertical_ = enabled;
    auto s = brush_.settings();
    s.symmetry_vertical = enabled;
    s.symmetry_axis_x = symmetry_axis_x_;
    brush_.set_settings(s);
    update();
  }
}

void Canvas::toggle_symmetry_vertical() {
  set_symmetry_vertical(!symmetry_vertical_);
}

void Canvas::set_symmetry_horizontal(bool enabled) {
  if (symmetry_horizontal_ != enabled) {
    symmetry_horizontal_ = enabled;
    auto s = brush_.settings();
    s.symmetry_horizontal = enabled;
    s.symmetry_axis_y = symmetry_axis_y_;
    brush_.set_settings(s);
    update();
  }
}

void Canvas::toggle_symmetry_horizontal() {
  set_symmetry_horizontal(!symmetry_horizontal_);
}

void Canvas::set_symmetry_axis_x(double x) {
  symmetry_axis_x_ = x;
  auto s = brush_.settings();
  s.symmetry_axis_x = x;
  brush_.set_settings(s);
  if (symmetry_vertical_) update();
}

void Canvas::set_symmetry_axis_y(double y) {
  symmetry_axis_y_ = y;
  auto s = brush_.settings();
  s.symmetry_axis_y = y;
  brush_.set_settings(s);
  if (symmetry_horizontal_) update();
}

void Canvas::set_dark_theme(bool dark) {
  if (is_dark_theme_ != dark) {
    is_dark_theme_ = dark;
    update();
  }
}

void Canvas::set_gl_enabled(bool enabled) {
  if (gl_enabled_ != enabled) {
    gl_enabled_ = enabled;
    if (enabled) force_composite_ = true;
    update();
  }
}

void Canvas::update_transform(int w, int h) {
  if (w <= 0 || h <= 0) return;
  const double available_width = std::max(1.0, static_cast<double>(w) - 2.0 * kCanvasMargin);
  const double available_height = std::max(1.0, static_cast<double>(h) - 2.0 * kCanvasMargin);
  const double document_width = static_cast<double>(document_.width());
  const double document_height = static_cast<double>(document_.height());

  fit_scale_ = std::min(available_width / document_width, available_height / document_height);
  fit_scale_ = std::clamp(fit_scale_, 0.02, 4.0);
  view_scale_ = std::clamp(fit_scale_ * zoom_, 0.01, 16.0);

  const double scaled_width = document_width * view_scale_;
  const double scaled_height = document_height * view_scale_;
  const double visible_width = std::min(kMinVisibleDocument, scaled_width);
  const double visible_height = std::min(kMinVisibleDocument, scaled_height);
  const double center_x = (static_cast<double>(w) - scaled_width) * 0.5;
  const double center_y = (static_cast<double>(h) - scaled_height) * 0.5;
  const double min_pan_x = visible_width - scaled_width - center_x;
  const double max_pan_x = static_cast<double>(w) - visible_width - center_x;
  pan_x_ = (min_pan_x <= max_pan_x) ? std::clamp(pan_x_, min_pan_x, max_pan_x) : 0.0;

  const double min_pan_y = visible_height - scaled_height - center_y;
  const double max_pan_y = static_cast<double>(h) - visible_height - center_y;
  pan_y_ = (min_pan_y <= max_pan_y) ? std::clamp(pan_y_, min_pan_y, max_pan_y) : 0.0;

  pivot_x_ = static_cast<double>(w) * 0.5 + pan_x_;
  pivot_y_ = static_cast<double>(h) * 0.5 + pan_y_;

  view_x_ = pivot_x_ - scaled_width * 0.5;
  view_y_ = pivot_y_ - scaled_height * 0.5;

  const double percent = view_scale_ * 100.0;
  if (std::abs(percent - last_notified_zoom_) > 0.01) {
    last_notified_zoom_ = percent;
    emit zoomChanged(percent);
  }
}

void Canvas::zoom_at(double factor, double widget_x, double widget_y) {
  const int w = width();
  const int h = height();
  if (w <= 0 || h <= 0 || factor <= 0.0) return;

  update_transform(w, h);
  const double document_x = (widget_x - view_x_) / view_scale_;
  const double document_y = (widget_y - view_y_) / view_scale_;

  const double new_zoom = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
  if (std::abs(new_zoom - zoom_) < 1e-9) return;

  zoom_ = new_zoom;
  update_transform(w, h);
  pan_x_ += widget_x - (view_x_ + document_x * view_scale_);
  pan_y_ += widget_y - (view_y_ + document_y * view_scale_);
  update_transform(w, h);
  update();
}

void Canvas::pan_by(double dx, double dy) {
  if (dx == 0.0 && dy == 0.0) return;
  pan_x_ += dx;
  pan_y_ += dy;
  update_transform(width(), height());
  update();
}

void Canvas::start_pan(double widget_x, double widget_y) {
  panning_ = true;
  pan_last_x_ = widget_x;
  pan_last_y_ = widget_y;
  setCursor(Qt::ClosedHandCursor);
}

void Canvas::stop_pan() {
  panning_ = false;
  setCursor(space_held_ ? Qt::OpenHandCursor : Qt::CrossCursor);
}

void Canvas::start_rotate(double x, double y) {
  rotate_active_ = true;
  rotate_start_x_ = x;
  rotate_start_y_ = y;
  rotate_start_angle_ = view_rotation_;
}

void Canvas::update_rotate(double x, double y) {
  if (!rotate_active_) return;
  update_transform(width(), height());
  const double start_ang = std::atan2(rotate_start_y_ - pivot_y_, rotate_start_x_ - pivot_x_);
  const double cur_ang = std::atan2(y - pivot_y_, x - pivot_x_);
  set_rotation(rotate_start_angle_ + (cur_ang - start_ang));
  update();
}

void Canvas::stop_rotate() {
  rotate_active_ = false;
}

void Canvas::remember_cursor(double widget_x, double widget_y) {
  cursor_widget_x_ = widget_x;
  cursor_widget_y_ = widget_y;
  cursor_widget_valid_ = true;
}

void Canvas::emit_cursor(double widget_x, double widget_y, const InputSample &input, bool contact) {
  remember_cursor(widget_x, widget_y);
  brush::Point doc_pt;
  const bool inside = map_to_document(widget_x, widget_y, doc_pt);
  cursor_inside_ = inside;
  cursor_x_ = doc_pt.x;
  cursor_y_ = doc_pt.y;
  cursor_tilt_x_ = input.tilt_x;
  cursor_tilt_y_ = input.tilt_y;

  PointerInfo info;
  info.x = doc_pt.x;
  info.y = doc_pt.y;
  info.pressure = contact ? input.pressure : 0.0;
  info.tilt_x = input.tilt_x;
  info.tilt_y = input.tilt_y;
  info.has_pressure = input.has_pressure;
  info.has_tilt = input.has_tilt;
  emit cursorMoved(info);
}

bool Canvas::effective_eraser() const {
  return tool_ == Tool::ERASER || eraser_mode_ || eraser_tip_active_ || barrel_eraser_;
}

void Canvas::update_brush_mode() {
  auto s = brush_.settings();
  if (effective_eraser()) {
    s.mode = brush::Mode::ERASE;
  } else if (tool_ == Tool::SMUDGE) {
    s.mode = brush::Mode::SMUDGE;
  } else {
    s.mode = brush::Mode::PAINT;
  }
  brush_.set_settings(s);
}

void Canvas::finish_stroke() {
  if (stroke_active_) {
    if (selection_layer_mode_) {
      brush_.end(selection_image_);
      stroke_active_ = false;
      document_.commit_change();
      sync_selection_mask_from_image();
      brush_.set_selection(nullptr);
      emit documentChanged();
      update();
    } else if (stroke_gpu_active_) {
      makeCurrent();
      std::vector<brush::BrushDab> final_dabs;
      brush_.end_dabs(final_dabs);
      if (!final_dabs.empty()) {
        expand_stroke_bbox(final_dabs);
        gl_renderer_.render_brush_dabs(final_dabs, document_.width(), document_.height(),
                                       brush_.settings(), brush_.selection());
      }

      const int doc_w = document_.width();
      const int doc_h = document_.height();

      const int bx = std::max(0, (stroke_min_x_ / core::kTileSize) * core::kTileSize);
      const int by = std::max(0, (stroke_min_y_ / core::kTileSize) * core::kTileSize);
      const int bw = std::min(doc_w - bx, ((stroke_max_x_ + core::kTileSize - 1) / core::kTileSize) * core::kTileSize - bx);
      const int bh = std::min(doc_h - by, ((stroke_max_y_ + core::kTileSize - 1) / core::kTileSize) * core::kTileSize - by);

      if (bw > 0 && bh > 0 && stroke_max_x_ >= stroke_min_x_ && stroke_max_y_ >= stroke_min_y_) {
        std::vector<core::Rgba8> readback_pixels;
        gl_renderer_.bake_and_readback_stroke(stroke_layer_id_, stroke_mode_,
                                              bx, by, bw, bh, readback_pixels,
                                              brush_.settings().opacity,
                                              render::OpenGLRenderer::blend_mode_to_int(brush_.settings().composite_op));

        if (readback_pixels.size() == static_cast<std::size_t>(bw) * bh) {
          auto &layer_img = document_.active_layer().image();
          const int start_tx = bx / core::kTileSize;
          const int end_tx = (bx + bw - 1) / core::kTileSize;
          const int start_ty = by / core::kTileSize;
          const int end_ty = (by + bh - 1) / core::kTileSize;

          for (int ty = start_ty; ty <= end_ty; ++ty) {
            for (int tx = start_tx; tx <= end_tx; ++tx) {
              auto *tile = layer_img.writable_tile_at(tx, ty);
              const int tile_x0 = tx * core::kTileSize;
              const int tile_y0 = ty * core::kTileSize;
              const int x_start = std::max(bx, tile_x0);
              const int x_end = std::min(bx + bw, std::min(doc_w, tile_x0 + core::kTileSize));
              const int y_start = std::max(by, tile_y0);
              const int y_end = std::min(by + bh, std::min(doc_h, tile_y0 + core::kTileSize));

              for (int y = y_start; y < y_end; ++y) {
                const int ly = y - tile_y0;
                const int ry = y - by;
                for (int x = x_start; x < x_end; ++x) {
                  const int lx = x - tile_x0;
                  const int rx = x - bx;
                  tile->pixels[ly * core::kTileSize + lx] = readback_pixels[ry * bw + rx];
                }
              }
              layer_img.mark_tile_dirty(tx, ty);
            }
          }
        }
      }

      gl_renderer_.clear_stroke();
      doneCurrent();

      stroke_active_ = false;
      stroke_gpu_active_ = false;
      document_.mark_composite_dirty();
      document_.commit_change();
      emit documentChanged();
      update();
    } else {
      brush_.end(document_.active_layer().image());
      stroke_active_ = false;
      document_.mark_composite_dirty();
      document_.commit_change();
      emit documentChanged();
      update();
    }
    std::vector<std::uint64_t> dirty_keys;
    std::vector<io::CapturedTile> captured_tiles;
    if (document_.has_active_layer()) {
      auto &layer_img = document_.active_layer().image();
      for (std::uint64_t key : layer_img.dirty_tiles()) {
        dirty_keys.push_back(key);
        int tx = core::TiledImage::key_x(key);
        int ty = core::TiledImage::key_y(key);
        io::CapturedTile cap;
        cap.tx = tx;
        cap.ty = ty;
        const auto *t = layer_img.tile_at(tx, ty);
        if (t) {
          cap.tile = std::make_shared<core::Tile>(*t);
        }
        captured_tiles.push_back(std::move(cap));
      }
    }

    const std::uint64_t commit_ms = core::ProjectTracker::current_timestamp_ms();
    tracker_.commit_stroke(dirty_keys, commit_ms);
    if (!captured_tiles.empty()) {
      wal_journal_.record_tile_diff(document_.has_active_layer() ? document_.active_layer().id() : 0,
                                   captured_tiles, commit_ms);
    }
    capture_timelapse_frame(false);
  }
}

void Canvas::set_selection_layer_mode(bool active) {
  if (selection_layer_mode_ == active) return;
  selection_layer_mode_ = active;
  if (selection_layer_mode_) {
    if (document_.has_floating_selection()) {
      document_.commit_move_selection();
    }
    user_brush_color_ = brush_.settings().color;
    brush_.set_selection(nullptr);
    sync_selection_image_from_mask();
    if (tool_ != Tool::BRUSH && tool_ != Tool::ERASER && tool_ != Tool::FILL &&
        tool_ != Tool::RECT_SELECT && tool_ != Tool::LASSO_SELECT) {
      set_tool(Tool::BRUSH);
    }
  } else {
    sync_selection_mask_from_image();
    brush_.set_selection(&document_.selection());
    auto s = brush_.settings();
    s.color = user_brush_color_;
    brush_.set_settings(s);
  }
  update();
}

void Canvas::sync_selection_image_from_mask() {
  const int w = document_.width();
  const int h = document_.height();
  if (selection_image_.width() != w || selection_image_.height() != h) {
    selection_image_ = core::TiledImage(w, h);
  }
  selection_image_.fill(core::Rgba8{0, 0, 0, 0});
  if (document_.selection().has_selection()) {
    const auto &b = document_.selection().bounds();
    for (int y = b.y; y < b.y + b.height; ++y) {
      for (int x = b.x; x < b.x + b.width; ++x) {
        const std::uint8_t m = document_.selection().value(x, y);
        if (m > 0) {
          selection_image_.set_pixel(x, y, core::Rgba8{255, 60, 150, m});
        }
      }
    }
  }
}

void Canvas::sync_selection_mask_from_image() {
  document_.selection().set_from_tiled_image(selection_image_);
}

Canvas::TransformDragMode Canvas::hit_test_transform(double wx, double wy) {
  if (tool_ != Tool::MOVE) return TransformDragMode::NONE;
  if (!document_.selection().has_selection() && !document_.has_floating_selection()) {
    return TransformDragMode::NONE;
  }
  const auto &b = document_.selection().bounds();
  if (b.width <= 0 || b.height <= 0) return TransformDragMode::NONE;

  const auto &fs = document_.floating_selection();
  const double px = document_.has_floating_selection() ? fs.pivot_x : (b.x + b.width / 2.0);
  const double py = document_.has_floating_selection() ? fs.pivot_y : (b.y + b.height / 2.0);
  const double off_x = document_.has_floating_selection() ? fs.offset_x : (moving_ ? (move_current_.x - move_start_.x) : 0.0);
  const double off_y = document_.has_floating_selection() ? fs.offset_y : (moving_ ? (move_current_.y - move_start_.y) : 0.0);
  const double rot = document_.has_floating_selection() ? fs.rotation : 0.0;
  const double sx = document_.has_floating_selection() ? fs.scale_x : 1.0;
  const double sy = document_.has_floating_selection() ? fs.scale_y : 1.0;

  const double cos_r = std::cos(rot);
  const double sin_r = std::sin(rot);

  auto transform_pt = [&](double x, double y) -> std::pair<double, double> {
    const double rel_x = (x - px) * sx;
    const double rel_y = (y - py) * sy;
    const double X = px + off_x + cos_r * rel_x - sin_r * rel_y;
    const double Y = py + off_y + sin_r * rel_x + cos_r * rel_y;
    return {X, Y};
  };

  const auto rot_doc = transform_pt(b.x + b.width / 2.0, b.y - 24.0 / std::max(0.001, view_scale_));
  double rot_wx = 0.0, rot_wy = 0.0;
  map_to_widget(rot_doc.first, rot_doc.second, rot_wx, rot_wy);
  if (std::hypot(wx - rot_wx, wy - rot_wy) <= 12.0) {
    return TransformDragMode::ROTATE;
  }

  const std::pair<double, double> corners[4] = {
      transform_pt(b.x, b.y),
      transform_pt(b.x + b.width, b.y),
      transform_pt(b.x + b.width, b.y + b.height),
      transform_pt(b.x, b.y + b.height)
  };

  for (int i = 0; i < 4; ++i) {
    double c_wx = 0.0, c_wy = 0.0;
    map_to_widget(corners[i].first, corners[i].second, c_wx, c_wy);
    const double dist = std::hypot(wx - c_wx, wy - c_wy);
    if (dist <= 8.0) {
      return TransformDragMode::SCALE;
    }
    if (dist <= 22.0) {
      return TransformDragMode::ROTATE;
    }
  }

  brush::Point doc_pt;
  if (map_to_document(wx, wy, doc_pt)) {
    const double dx = doc_pt.x - (px + off_x);
    const double dy = doc_pt.y - (py + off_y);
    const double rot_x = cos_r * dx + sin_r * dy;
    const double rot_y = -sin_r * dx + cos_r * dy;
    const double local_x = px + rot_x / (std::abs(sx) < 1e-5 ? 1e-5 : sx);
    const double local_y = py + rot_y / (std::abs(sy) < 1e-5 ? 1e-5 : sy);
    if (local_x >= b.x && local_x <= b.x + b.width &&
        local_y >= b.y && local_y <= b.y + b.height) {
      return TransformDragMode::TRANSLATE;
    }
  }

  return TransformDragMode::NONE;
}

void Canvas::update_move_tool_cursor(double wx, double wy) {
  if (tool_ != Tool::MOVE) {
    return;
  }
  if (!document_.selection().has_selection() && !document_.has_floating_selection()) {
    setCursor(Qt::ArrowCursor);
    return;
  }
  const auto &b = document_.selection().bounds();
  if (b.width <= 0 || b.height <= 0) {
    setCursor(Qt::ArrowCursor);
    return;
  }

  const auto &fs = document_.floating_selection();
  const double px = document_.has_floating_selection() ? fs.pivot_x : (b.x + b.width / 2.0);
  const double py = document_.has_floating_selection() ? fs.pivot_y : (b.y + b.height / 2.0);
  const double off_x = document_.has_floating_selection() ? fs.offset_x : (moving_ ? (move_current_.x - move_start_.x) : 0.0);
  const double off_y = document_.has_floating_selection() ? fs.offset_y : (moving_ ? (move_current_.y - move_start_.y) : 0.0);
  const double rot = document_.has_floating_selection() ? fs.rotation : 0.0;
  const double sx = document_.has_floating_selection() ? fs.scale_x : 1.0;
  const double sy = document_.has_floating_selection() ? fs.scale_y : 1.0;

  const double cos_r = std::cos(rot);
  const double sin_r = std::sin(rot);

  auto transform_pt = [&](double x, double y) -> std::pair<double, double> {
    const double rel_x = (x - px) * sx;
    const double rel_y = (y - py) * sy;
    const double X = px + off_x + cos_r * rel_x - sin_r * rel_y;
    const double Y = py + off_y + sin_r * rel_x + cos_r * rel_y;
    return {X, Y};
  };

  const auto rot_doc = transform_pt(b.x + b.width / 2.0, b.y - 24.0 / std::max(0.001, view_scale_));
  double rot_wx = 0.0, rot_wy = 0.0;
  map_to_widget(rot_doc.first, rot_doc.second, rot_wx, rot_wy);
  if (std::hypot(wx - rot_wx, wy - rot_wy) <= 12.0) {
    setCursor(Qt::PointingHandCursor);
    return;
  }

  const std::pair<double, double> corners[4] = {
      transform_pt(b.x, b.y),
      transform_pt(b.x + b.width, b.y),
      transform_pt(b.x + b.width, b.y + b.height),
      transform_pt(b.x, b.y + b.height)
  };

  for (int i = 0; i < 4; ++i) {
    double c_wx = 0.0, c_wy = 0.0;
    map_to_widget(corners[i].first, corners[i].second, c_wx, c_wy);
    const double dist = std::hypot(wx - c_wx, wy - c_wy);
    if (dist <= 8.0) {
      if (i == 0 || i == 2) {
        setCursor(Qt::SizeFDiagCursor);
      } else {
        setCursor(Qt::SizeBDiagCursor);
      }
      return;
    }
  }

  brush::Point doc_pt;
  if (map_to_document(wx, wy, doc_pt)) {
    const double dx = doc_pt.x - (px + off_x);
    const double dy = doc_pt.y - (py + off_y);
    const double rot_x = cos_r * dx + sin_r * dy;
    const double rot_y = -sin_r * dx + cos_r * dy;
    const double local_x = px + rot_x / (std::abs(sx) < 1e-5 ? 1e-5 : sx);
    const double local_y = py + rot_y / (std::abs(sy) < 1e-5 ? 1e-5 : sy);
    if (local_x >= b.x && local_x <= b.x + b.width &&
        local_y >= b.y && local_y <= b.y + b.height) {
      setCursor(Qt::SizeAllCursor);
      return;
    }
  }

  setCursor(Qt::ArrowCursor);
}

void Canvas::paintEvent(QPaintEvent *event) {
  if (!context() || !context()->isValid() || !gl_enabled_) {
    return;
  }
  QOpenGLWidget::paintEvent(event);
}

void Canvas::paint_cpu() {
  update_transform(width(), height());
  framebuffer_.sync(document_.composite_image());
  QPainter p(this);
  if (!p.isActive()) return;

  QImage img(reinterpret_cast<const uchar *>(framebuffer_.data()),
             framebuffer_.width(), framebuffer_.height(), framebuffer_.stride(),
             QImage::Format_ARGB32_Premultiplied);
  p.fillRect(rect(), is_dark_theme_ ? QColor(20, 21, 24) : QColor(226, 228, 232));
  p.save();
  p.translate(pivot_x_, pivot_y_);
  p.rotate(rotation_degrees());
  p.scale(mirror_horizontal_ ? -view_scale_ : view_scale_,
          mirror_vertical_ ? -view_scale_ : view_scale_);
  p.translate(-document_.width() * 0.5, -document_.height() * 0.5);

  const auto &app_settings = AppSettings::instance();
  const auto eff_mode = app_settings.effective_transparency_mode(is_dark_theme_);
  const QColor solid_qc = app_settings.transparency_solid_color();
  if (eff_mode == TransparencyMode::SOLID_COLOR) {
    p.fillRect(0, 0, document_.width(), document_.height(), solid_qc);
  } else {
    const int ch_sz = 12;
    const QColor c1 = (eff_mode == TransparencyMode::DARK_CHECKERBOARD)
                          ? QColor(56, 59, 66)
                          : QColor(250, 250, 252);
    const QColor c2 = (eff_mode == TransparencyMode::DARK_CHECKERBOARD)
                          ? QColor(41, 43, 48)
                          : QColor(224, 227, 232);
    for (int y = 0; y < document_.height(); y += ch_sz) {
      for (int x = 0; x < document_.width(); x += ch_sz) {
        const bool alt = ((x / ch_sz) + (y / ch_sz)) % 2 == 0;
        p.fillRect(x, y, ch_sz, ch_sz, alt ? c1 : c2);
      }
    }
  }
  p.drawImage(0, 0, img);
  p.restore();

  draw_overlays(p);
}

void Canvas::paintGL() {
  if (!context() || !context()->isValid() || !gl_enabled_) {
    return;
  }
  update_transform(width(), height());

  render::FloatingSelectionRenderInfo fs_info;
  if (document_.has_floating_selection()) {
    const auto &fs = document_.floating_selection();
    fs_info.active = true;
    fs_info.origin_x = fs.origin_x;
    fs_info.origin_y = fs.origin_y;
    fs_info.width = fs.width;
    fs_info.height = fs.height;
    fs_info.offset_x = fs.offset_x;
    fs_info.offset_y = fs.offset_y;
    fs_info.rotation = fs.rotation;
    fs_info.scale_x = fs.scale_x;
    fs_info.scale_y = fs.scale_y;
    fs_info.pivot_x = fs.pivot_x;
    fs_info.pivot_y = fs.pivot_y;
  }

  if (gl_enabled_ && gl_renderer_.is_initialized()) {
    if (fs_info.active) {
      const auto &fs = document_.floating_selection();
      gl_renderer_.sync_floating_selection(true, fs.width, fs.height,
                                           fs.pixels.data(), fs.mask.data(), pixelated_);
    } else {
      gl_renderer_.sync_floating_selection(false, 0, 0, nullptr, nullptr);
    }
    std::unordered_set<std::size_t> live_ids;
    bool layers_changed = false;
    for (std::size_t i = 0; i < document_.layer_count(); ++i) {
      auto &layer = document_.layer(i);
      if (layer.is_group()) continue;
      live_ids.insert(layer.id());
      if (!layer.image().gpu_dirty_tiles().empty()) layers_changed = true;
      gl_renderer_.sync_layer_tiles(layer.id(), layer.image());
    }
    gl_renderer_.retain_layer_textures(live_ids);

    // The CPU brush path (smudge, or color_rate < 0.999) writes straight into
    // layer tiles and never bumps the composite revision, so layers_changed is
    // load-bearing: without it those strokes upload but are never recomposited.
    const bool need_composite = force_composite_ || stroke_gpu_active_ || layers_changed ||
                                document_.composite_revision() != last_composite_revision_;
    if (need_composite) {
      gl_renderer_.composite_document(document_, stroke_gpu_active_,
                                      stroke_layer_id_, stroke_mode_,
                                      brush_.settings().opacity,
                                      render::OpenGLRenderer::blend_mode_to_int(brush_.settings().composite_op));
      last_composite_revision_ = document_.composite_revision();
      force_composite_ = false;
    }
    const auto &app_settings = AppSettings::instance();
    const auto eff_mode = app_settings.effective_transparency_mode(is_dark_theme_);
    int trans_mode_int = 1;
    if (eff_mode == TransparencyMode::LIGHT_CHECKERBOARD) trans_mode_int = 0;
    else if (eff_mode == TransparencyMode::DARK_CHECKERBOARD) trans_mode_int = 1;
    else if (eff_mode == TransparencyMode::SOLID_COLOR) trans_mode_int = 2;

    const QColor solid_qc = app_settings.transparency_solid_color();
    const core::Rgba8 trans_solid_color{
        static_cast<std::uint8_t>(solid_qc.red()),
        static_cast<std::uint8_t>(solid_qc.green()),
        static_cast<std::uint8_t>(solid_qc.blue()),
        255
    };

    const double logical_w = static_cast<double>(width());
    const double logical_h = static_cast<double>(height());
    int vp_w = width();
    int vp_h = height();
    if (use_device_pixels_ && width() > 0 && height() > 0) {
      vp_w = qRound(width() * devicePixelRatioF());
      vp_h = qRound(height() * devicePixelRatioF());
    }
    // Uniform scale from logical to viewport coordinates. Both axes use the
    // same ratio so fractional dpr cannot skew the projection.
    const double vp_ratio = (logical_w > 0.0) ? (static_cast<double>(vp_w) / logical_w) : 1.0;

    gl_renderer_.render(vp_w, vp_h, pivot_x_ * vp_ratio, pivot_y_ * vp_ratio,
                        view_scale_ * vp_ratio, view_rotation_,
                        mirror_horizontal_, mirror_vertical_, document_.width(), document_.height(),
                        pixelated_, show_grid_, grid_size_, grid_color_, fs_info, is_dark_theme_,
                        trans_mode_int, trans_solid_color);
  } else {
    // CPU Fallback
    framebuffer_.sync(document_.composite_image());
    QPainter p(this);
    QImage img(reinterpret_cast<const uchar *>(framebuffer_.data()),
               framebuffer_.width(), framebuffer_.height(), framebuffer_.stride(),
               QImage::Format_ARGB32_Premultiplied);
    p.fillRect(rect(), is_dark_theme_ ? QColor(20, 21, 24) : QColor(226, 228, 232));
    p.save();
    p.translate(pivot_x_, pivot_y_);
    p.rotate(rotation_degrees());
    p.scale(mirror_horizontal_ ? -view_scale_ : view_scale_,
            mirror_vertical_ ? -view_scale_ : view_scale_);
    p.translate(-document_.width() * 0.5, -document_.height() * 0.5);

    const auto &app_settings = AppSettings::instance();
    const auto eff_mode = app_settings.effective_transparency_mode(is_dark_theme_);
    const QColor solid_qc = app_settings.transparency_solid_color();
    if (eff_mode == TransparencyMode::SOLID_COLOR) {
      p.fillRect(0, 0, document_.width(), document_.height(), solid_qc);
    } else {
      const int ch_sz = 12;
      const QColor c1 = (eff_mode == TransparencyMode::DARK_CHECKERBOARD)
                            ? QColor(56, 59, 66)
                            : QColor(250, 250, 252);
      const QColor c2 = (eff_mode == TransparencyMode::DARK_CHECKERBOARD)
                            ? QColor(41, 43, 48)
                            : QColor(224, 227, 232);
      for (int y = 0; y < document_.height(); y += ch_sz) {
        for (int x = 0; x < document_.width(); x += ch_sz) {
          const bool alt = ((x / ch_sz) + (y / ch_sz)) % 2 == 0;
          p.fillRect(x, y, ch_sz, ch_sz, alt ? c1 : c2);
        }
      }
    }
    p.drawImage(0, 0, img);
    p.restore();
  }

  // Draw 2D Overlays on top of OpenGL framebuffer
  QPainter overlayPainter(this);
  overlayPainter.setRenderHint(QPainter::Antialiasing, true);
  draw_overlays(overlayPainter);
}

void Canvas::draw_overlays(QPainter &painter) {
  const QColor accent_overlay = palette().color(QPalette::Highlight);
  const QColor guide_color(accent_overlay.red(), accent_overlay.green(), accent_overlay.blue(), 180);

  // 1. Symmetry Axis Guides
  if (symmetry_vertical_) {
    double wx1, wy1, wx2, wy2;
    map_to_widget(symmetry_axis_x_, 0.0, wx1, wy1);
    map_to_widget(symmetry_axis_x_, document_.height(), wx2, wy2);
    painter.setPen(QPen(guide_color, 1.5, Qt::DashLine));
    painter.drawLine(QPointF(wx1, wy1), QPointF(wx2, wy2));
  }
  if (symmetry_horizontal_) {
    double wx1, wy1, wx2, wy2;
    map_to_widget(0.0, symmetry_axis_y_, wx1, wy1);
    map_to_widget(document_.width(), symmetry_axis_y_, wx2, wy2);
    painter.setPen(QPen(guide_color, 1.5, Qt::DashLine));
    painter.drawLine(QPointF(wx1, wy1), QPointF(wx2, wy2));
  }

  // 2. Selection Marquee / Marching Ants & Transform Handles
  if (!selection_layer_mode_ && (document_.selection().has_selection() || document_.has_floating_selection())) {
    const auto &fs = document_.floating_selection();
    const auto &b = document_.selection().bounds();
    if (b.width > 0 && b.height > 0) {
      const double px = document_.has_floating_selection() ? fs.pivot_x : (b.x + b.width / 2.0);
      const double py = document_.has_floating_selection() ? fs.pivot_y : (b.y + b.height / 2.0);
      const double off_x = document_.has_floating_selection() ? fs.offset_x : (moving_ ? (move_current_.x - move_start_.x) : 0.0);
      const double off_y = document_.has_floating_selection() ? fs.offset_y : (moving_ ? (move_current_.y - move_start_.y) : 0.0);
      const double rot = document_.has_floating_selection() ? fs.rotation : 0.0;
      const double sx = document_.has_floating_selection() ? fs.scale_x : 1.0;
      const double sy = document_.has_floating_selection() ? fs.scale_y : 1.0;

      const double cos_r = std::cos(rot);
      const double sin_r = std::sin(rot);

      auto transform_doc_pt = [&](double x, double y) -> QPointF {
        const double rel_x = (x - px) * sx;
        const double rel_y = (y - py) * sy;
        const double X = px + off_x + cos_r * rel_x - sin_r * rel_y;
        const double Y = py + off_y + sin_r * rel_x + cos_r * rel_y;
        double wx, wy;
        map_to_widget(X, Y, wx, wy);
        return QPointF(wx, wy);
      };

      const auto &pts = document_.selection().lasso_points();
      QPolygonF ants_poly;
      if (pts.size() >= 3) {
        for (const auto &pt : pts) {
          ants_poly << transform_doc_pt(pt.first, pt.second);
        }
        painter.setPen(QPen(Qt::black, 1.2));
        painter.drawPolygon(ants_poly);

        QPen dashPen(Qt::white, 1.2, Qt::CustomDashLine);
        dashPen.setDashPattern({4, 4});
        dashPen.setDashOffset(march_offset_);
        painter.setPen(dashPen);
        painter.drawPolygon(ants_poly);
      } else {
        // Arbitrary raster mask (Selection Layer, Magic Wand, Rect):
        // Trace horizontal and vertical boundary segments directly from the mask,
        // correctly handling multiple disjoint islands, brush strokes, and inner holes.
        const int doc_w = document_.width();
        const int doc_h = document_.height();
        const auto &mask_data = document_.selection().data();
        if (!mask_data.empty() && static_cast<int>(mask_data.size()) >= doc_w * doc_h) {
          auto is_sel = [&](int x, int y) -> bool {
            if (x < b.x || y < b.y || x >= b.x + b.width || y >= b.y + b.height ||
                x < 0 || y < 0 || x >= doc_w || y >= doc_h) return false;
            return mask_data[static_cast<std::size_t>(y) * doc_w + x] >= 128;
          };

          QPainterPath boundary_path;

          for (int y = b.y; y <= b.y + b.height; ++y) {
            int seg_start = -1;
            int seg_type = 0;
            for (int x = b.x; x <= b.x + b.width; ++x) {
              const bool above = is_sel(x, y - 1);
              const bool below = is_sel(x, y);
              int edge = 0;
              if (!above && below) edge = 1;
              else if (above && !below) edge = 2;

              if (edge == seg_type && edge != 0) {
                // extend segment
              } else {
                if (seg_start != -1) {
                  boundary_path.moveTo(transform_doc_pt(seg_start, y));
                  boundary_path.lineTo(transform_doc_pt(x, y));
                  seg_start = -1;
                  seg_type = 0;
                }
                if (edge != 0) {
                  seg_start = x;
                  seg_type = edge;
                }
              }
            }
            if (seg_start != -1) {
              boundary_path.moveTo(transform_doc_pt(seg_start, y));
              boundary_path.lineTo(transform_doc_pt(b.x + b.width, y));
            }
          }

          for (int x = b.x; x <= b.x + b.width; ++x) {
            int seg_start = -1;
            int seg_type = 0;
            for (int y = b.y; y <= b.y + b.height; ++y) {
              const bool left = is_sel(x - 1, y);
              const bool right = is_sel(x, y);
              int edge = 0;
              if (!left && right) edge = 1;
              else if (left && !right) edge = 2;

              if (edge == seg_type && edge != 0) {
                // extend
              } else {
                if (seg_start != -1) {
                  boundary_path.moveTo(transform_doc_pt(x, seg_start));
                  boundary_path.lineTo(transform_doc_pt(x, y));
                  seg_start = -1;
                  seg_type = 0;
                }
                if (edge != 0) {
                  seg_start = y;
                  seg_type = edge;
                }
              }
            }
            if (seg_start != -1) {
              boundary_path.moveTo(transform_doc_pt(x, seg_start));
              boundary_path.lineTo(transform_doc_pt(x, b.y + b.height));
            }
          }

          painter.setPen(QPen(Qt::black, 1.2));
          painter.drawPath(boundary_path);

          QPen dashPen(Qt::white, 1.2, Qt::CustomDashLine);
          dashPen.setDashPattern({4, 4});
          dashPen.setDashOffset(march_offset_);
          painter.setPen(dashPen);
          painter.drawPath(boundary_path);
        } else {
          ants_poly << transform_doc_pt(b.x, b.y)
                    << transform_doc_pt(b.x + b.width, b.y)
                    << transform_doc_pt(b.x + b.width, b.y + b.height)
                    << transform_doc_pt(b.x, b.y + b.height);
          painter.setPen(QPen(Qt::black, 1.2));
          painter.drawPolygon(ants_poly);
          QPen dashPen(Qt::white, 1.2, Qt::CustomDashLine);
          dashPen.setDashPattern({4, 4});
          dashPen.setDashOffset(march_offset_);
          painter.setPen(dashPen);
          painter.drawPolygon(ants_poly);
        }
      }

      // Draw Tool::MOVE handles
      if (tool_ == Tool::MOVE) {
        const double handle_dist = 24.0 / std::max(0.001, view_scale_);
        const QPointF top_mid = transform_doc_pt(b.x + b.width / 2.0, b.y);
        const QPointF rot_pt = transform_doc_pt(b.x + b.width / 2.0, b.y - handle_dist);

        // Rotation stem
        painter.setPen(QPen(accent_overlay, 1.5));
        painter.drawLine(top_mid, rot_pt);

        // Rotation knob
        painter.setBrush(Qt::white);
        painter.drawEllipse(rot_pt, 5.0, 5.0);

        // 4 corner scale handles
        const QPointF corners[4] = {
            transform_doc_pt(b.x, b.y),
            transform_doc_pt(b.x + b.width, b.y),
            transform_doc_pt(b.x + b.width, b.y + b.height),
            transform_doc_pt(b.x, b.y + b.height)
        };
        for (int i = 0; i < 4; ++i) {
          painter.setPen(QPen(accent_overlay, 1.5));
          painter.setBrush(Qt::white);
          painter.drawRect(QRectF(corners[i].x() - 4.0, corners[i].y() - 4.0, 8.0, 8.0));
        }

        // Pivot crosshair
        const QPointF p_pt = transform_doc_pt(px, py);
        painter.setPen(QPen(accent_overlay, 1.5));
        painter.drawLine(p_pt.x() - 6.0, p_pt.y(), p_pt.x() + 6.0, p_pt.y());
        painter.drawLine(p_pt.x(), p_pt.y() - 6.0, p_pt.x(), p_pt.y() + 6.0);
        painter.setBrush(Qt::NoBrush);
      }
      painter.setBrush(Qt::NoBrush);
    }
  }

  // 3. Selection Layer Pink Overlay
  if (selection_layer_mode_) {
    painter.save();
    painter.translate(pivot_x_, pivot_y_);
    painter.rotate(rotation_degrees());
    painter.scale(mirror_horizontal_ ? -view_scale_ : view_scale_,
                  mirror_vertical_ ? -view_scale_ : view_scale_);
    painter.translate(-document_.width() * 0.5, -document_.height() * 0.5);

    auto draw_tiles = [&](const core::TiledImage &img) {
      for (const auto &[key, tile] : img.tiles()) {
        const int tx = core::TiledImage::key_x(key);
        const int ty = core::TiledImage::key_y(key);
        QImage tile_img(core::kTileSize, core::kTileSize, QImage::Format_ARGB32_Premultiplied);
        tile_img.fill(Qt::transparent);
        for (int ly = 0; ly < core::kTileSize; ++ly) {
          for (int lx = 0; lx < core::kTileSize; ++lx) {
            const auto &px = tile->pixels[ly * core::kTileSize + lx];
            if (px.a > 0) {
              tile_img.setPixelColor(lx, ly, QColor(244, 63, 94, 110));
            }
          }
        }
        painter.drawImage(tx * core::kTileSize, ty * core::kTileSize, tile_img);
      }
    };

    draw_tiles(selection_image_);
    if (stroke_active_ && brush_.stroke_buffer()) {
      draw_tiles(*brush_.stroke_buffer());
    }
    painter.restore();
  }

  // 4. Rectangular Selection Drag Preview
  if (selecting_ && tool_ == Tool::RECT_SELECT) {
    const double rx = std::min(select_start_.x, select_current_.x);
    const double ry = std::min(select_start_.y, select_current_.y);
    const double rw = std::abs(select_current_.x - select_start_.x);
    const double rh = std::abs(select_current_.y - select_start_.y);

    double wx1, wy1, wx2, wy2, wx3, wy3, wx4, wy4;
    map_to_widget(rx, ry, wx1, wy1);
    map_to_widget(rx + rw, ry, wx2, wy2);
    map_to_widget(rx + rw, ry + rh, wx3, wy3);
    map_to_widget(rx, ry + rh, wx4, wy4);

    QPolygonF poly;
    poly << QPointF(wx1, wy1) << QPointF(wx2, wy2) << QPointF(wx3, wy3) << QPointF(wx4, wy4);

    painter.setPen(QPen(Qt::black, 1.2));
    painter.drawPolygon(poly);
    QPen dashPen(Qt::white, 1.2, Qt::CustomDashLine);
    dashPen.setDashPattern({4, 4});
    dashPen.setDashOffset(march_offset_);
    painter.setPen(dashPen);
    painter.drawPolygon(poly);
  }

  if (selecting_ && tool_ == Tool::ELLIPSE_SELECT) {
    const double rx = std::min(select_start_.x, select_current_.x);
    const double ry = std::min(select_start_.y, select_current_.y);
    const double rw = std::abs(select_current_.x - select_start_.x);
    const double rh = std::abs(select_current_.y - select_start_.y);

    constexpr int kSteps = 48;
    QPolygonF poly;
    const double cx = rx + rw * 0.5;
    const double cy = ry + rh * 0.5;
    const double rad_x = rw * 0.5;
    const double rad_y = rh * 0.5;
    for (int i = 0; i < kSteps; ++i) {
      const double theta = (2.0 * std::numbers::pi * i) / kSteps;
      const double dx = cx + rad_x * std::cos(theta);
      const double dy = cy + rad_y * std::sin(theta);
      double wx, wy;
      map_to_widget(dx, dy, wx, wy);
      poly << QPointF(wx, wy);
    }

    painter.setPen(QPen(Qt::black, 1.2));
    painter.drawPolygon(poly);
    QPen dashPen(Qt::white, 1.2, Qt::CustomDashLine);
    dashPen.setDashPattern({4, 4});
    dashPen.setDashOffset(march_offset_);
    painter.setPen(dashPen);
    painter.drawPolygon(poly);
  }

  // 5. Lasso Selection Polyline
  if (selecting_ && tool_ == Tool::LASSO_SELECT && current_lasso_points_.size() > 1) {
    QPainterPath path;
    for (std::size_t i = 0; i < current_lasso_points_.size(); ++i) {
      double wx, wy;
      map_to_widget(current_lasso_points_[i].first, current_lasso_points_[i].second, wx, wy);
      if (i == 0) path.moveTo(wx, wy);
      else path.lineTo(wx, wy);
    }
    painter.setPen(QPen(Qt::black, 1.2));
    painter.drawPath(path);
    QPen dashPen(Qt::white, 1.2, Qt::CustomDashLine);
    dashPen.setDashPattern({4, 4});
    dashPen.setDashOffset(march_offset_);
    painter.setPen(dashPen);
    painter.drawPath(path);
  }

  // 5. Cursor ring & Stabilizer leash
  painter.setBrush(Qt::NoBrush);
  const bool is_brush_tool = (tool_ == Tool::BRUSH || tool_ == Tool::ERASER || tool_ == Tool::SMUDGE);
  if (cursor_inside_ && !panning_ && is_brush_tool) {
    double cx, cy;
    map_to_widget(cursor_x_, cursor_y_, cx, cy);
    const double radius = (brush_.settings().size * 0.5) * view_scale_;

    // Stabilizer leash line
    if (stroke_active_ && brush_.settings().smoothing_mode == brush::SmoothingMode::STABILIZER) {
      double lx, ly;
      map_to_widget(last_stroke_input_.x, last_stroke_input_.y, lx, ly);
      painter.setPen(QPen(QColor(220, 50, 60, 200), 1.5));
      painter.drawLine(QPointF(lx, ly), QPointF(cx, cy));
    }

    // Brush cursor ring
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1.8));
    painter.drawEllipse(QPointF(cx, cy), radius, radius);
    painter.setPen(QPen(accent_overlay, 1.2));
    painter.drawEllipse(QPointF(cx, cy), radius - 0.5, radius - 0.5);
  }
}

void Canvas::handle_pointer_press(const PointerEvent &ev) {
  setFocus();
  const QPointF pos = ev.pos;

  if (ev.button == Qt::MiddleButton || (space_held_ && ev.button == Qt::LeftButton)) {
    if (ev.modifiers.testFlag(Qt::ControlModifier)) {
      start_rotate(pos.x(), pos.y());
    } else {
      start_pan(pos.x(), pos.y());
    }
    return;
  }

  // Ctrl + pen/drag gestures: vertical slide zooms, circular drag / barrel twist rotates
  if (ev.modifiers.testFlag(Qt::ControlModifier) && (ev.button == Qt::LeftButton || (ev.buttons & Qt::LeftButton))) {
    pen_gesture_active_ = true;
    pen_gesture_start_ = pos;
    pen_gesture_last_ = pos;
    pen_gesture_mode_ = PenGestureMode::NONE;
    pen_gesture_initial_twist_ = ev.sample.rotation;
    pen_gesture_has_twist_ = ev.sample.has_rotation;
    rotate_start_x_ = pos.x();
    rotate_start_y_ = pos.y();
    rotate_start_angle_ = view_rotation_;
    return;
  }

  if (ev.button == Qt::RightButton) {
    emit quickMenuRequested(pos.x(), pos.y());
    return;
  }

  if (ev.button != Qt::LeftButton && !(ev.buttons & Qt::LeftButton)) {
    return;
  }

  brush::Point doc_pt;
  const bool is_select_tool = (tool_ == Tool::RECT_SELECT || tool_ == Tool::LASSO_SELECT || tool_ == Tool::ELLIPSE_SELECT);
  if (is_select_tool) {
    map_to_document_clamped(pos.x(), pos.y(), doc_pt);
  } else {
    if (!map_to_document(pos.x(), pos.y(), doc_pt)) return;
  }

  if (tool_ == Tool::COLOR_PICKER) {
    emit colorPicked(document_.composite_image().pixel(static_cast<int>(doc_pt.x),
                                                       static_cast<int>(doc_pt.y)));
    return;
  } else if (tool_ == Tool::FILL) {
    if (!document_.has_active_layer()) {
      document_.auto_select_top_layer();
      emit activeLayerAutoSelected(document_.active_layer_index());
    }
    if (!document_.active_layer().visible()) return;
    if (document_.has_floating_selection()) document_.commit_move_selection();
    document_.flood_fill(static_cast<int>(doc_pt.x), static_cast<int>(doc_pt.y), brush_.settings().color);
    refresh();
    emit documentChanged();
    return;
  } else if (tool_ == Tool::RECT_SELECT || tool_ == Tool::ELLIPSE_SELECT) {
    selecting_ = true;
    select_start_ = doc_pt;
    select_current_ = doc_pt;
  } else if (tool_ == Tool::LASSO_SELECT) {
    selecting_ = true;
    current_lasso_points_.clear();
    current_lasso_points_.emplace_back(doc_pt.x, doc_pt.y);
  } else if (tool_ == Tool::MAGIC_WAND) {
    core::SelectionCombineMode mode = core::SelectionCombineMode::REPLACE;
    const bool shift = ev.modifiers.testFlag(Qt::ShiftModifier);
    const bool alt = ev.modifiers.testFlag(Qt::AltModifier);
    if (shift && alt) {
      mode = core::SelectionCombineMode::INTERSECT;
    } else if (shift) {
      mode = core::SelectionCombineMode::ADD;
    } else if (alt) {
      mode = core::SelectionCombineMode::SUBTRACT;
    }
    if ((ev.modifiers & ~Qt::KeypadModifier) == Qt::NoModifier) {
      mode = selection_combine_mode_;
    }
    document_.flood_select(static_cast<int>(doc_pt.x), static_cast<int>(doc_pt.y),
                           wand_tolerance_, wand_contiguous_, mode);
    brush_.set_selection(&document_.selection());
    refresh();
    return;
  } else if (tool_ == Tool::MOVE) {
    const TransformDragMode hit = hit_test_transform(pos.x(), pos.y());
    if (document_.has_floating_selection() && hit != TransformDragMode::NONE) {
      transform_drag_mode_ = hit;
      const auto &fs = document_.floating_selection();
      initial_offset_x_ = fs.offset_x;
      initial_offset_y_ = fs.offset_y;
      initial_rotation_ = fs.rotation;
      initial_scale_x_ = fs.scale_x;
      initial_scale_y_ = fs.scale_y;
      moving_ = true;
      move_start_ = doc_pt;
      move_current_ = doc_pt;
    } else {
      if (document_.has_floating_selection()) {
        document_.commit_move_selection();
      }
      if (move_auto_selected_) {
        document_.selection().clear();
        move_auto_selected_ = false;
      }

      const auto hit_layer = document_.find_layer_at(static_cast<int>(doc_pt.x), static_cast<int>(doc_pt.y));
      if (hit_layer.has_value()) {
        document_.set_active_layer_index(*hit_layer);
        emit activeLayerAutoSelected(*hit_layer);
      } else if (!document_.has_active_layer()) {
        const auto top_idx = document_.auto_select_top_layer();
        emit activeLayerAutoSelected(top_idx);
      }

      if (document_.has_active_layer() && document_.active_layer().visible()) {
        if (!document_.selection().has_selection()) {
          const auto bounds = document_.active_layer().image().content_bounds();
          if (bounds.has_value()) {
            move_auto_selected_ = true;
            document_.selection().set_rect(bounds->x, bounds->y, bounds->width, bounds->height);
            brush_.set_selection(&document_.selection());
            document_.start_move_selection();
            transform_drag_mode_ = TransformDragMode::TRANSLATE;
            const auto &fs = document_.floating_selection();
            initial_offset_x_ = fs.offset_x;
            initial_offset_y_ = fs.offset_y;
            initial_rotation_ = fs.rotation;
            initial_scale_x_ = fs.scale_x;
            initial_scale_y_ = fs.scale_y;
            moving_ = true;
            move_start_ = doc_pt;
            move_current_ = doc_pt;
          }
        } else {
          if (!document_.has_floating_selection()) {
            document_.start_move_selection();
          }
          transform_drag_mode_ = (hit != TransformDragMode::NONE) ? hit : TransformDragMode::TRANSLATE;
          const auto &fs = document_.floating_selection();
          initial_offset_x_ = fs.offset_x;
          initial_offset_y_ = fs.offset_y;
          initial_rotation_ = fs.rotation;
          initial_scale_x_ = fs.scale_x;
          initial_scale_y_ = fs.scale_y;
          moving_ = true;
          move_start_ = doc_pt;
          move_current_ = doc_pt;
        }
      }
    }
  } else {
    if (!document_.has_active_layer() && !selection_layer_mode_) {
      document_.auto_select_top_layer();
      emit activeLayerAutoSelected(document_.active_layer_index());
    }
    if (!document_.active_layer().visible() || document_.active_layer().locked()) {
      return;
    }
    if (document_.has_floating_selection()) {
      document_.commit_move_selection();
    }
    if (stroke_history_enabled_) {
      document_.begin_change();
    }
    stroke_active_ = true;
    last_stroke_input_ = doc_pt;
    stroke_min_x_ = static_cast<int>(doc_pt.x);
    stroke_min_y_ = static_cast<int>(doc_pt.y);
    stroke_max_x_ = stroke_min_x_;
    stroke_max_y_ = stroke_min_y_;
    core::TrackedBrushSettings bs;
    bs.mode = static_cast<int>(brush_.settings().mode);
    bs.size = brush_.settings().size;
    bs.opacity = brush_.settings().opacity;
    bs.flow = brush_.settings().flow;
    bs.hardness = brush_.settings().hardness;
    bs.spacing = brush_.settings().spacing;
    bs.composite_op = brush_.settings().composite_op;
    bs.color = brush_.settings().color;

    const std::uint64_t stroke_now_ms = core::ProjectTracker::current_timestamp_ms();
    stroke_start_time_ms_ = stroke_now_ms;
    tracker_.begin_stroke(document_.has_active_layer() ? document_.active_layer().id() : 0, bs, stroke_now_ms);
    tracker_.append_sample({doc_pt.x, doc_pt.y, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y, stroke_now_ms});

    if (selection_layer_mode_) {
      auto s = brush_.settings();
      if (effective_eraser()) {
        s.mode = brush::Mode::ERASE;
      } else {
        s.mode = brush::Mode::PAINT;
        s.color = core::Rgba8{244, 63, 94, 255};
      }
      brush_.set_settings(s);
      brush_.set_selection(nullptr);
      brush_.begin(selection_image_, doc_pt, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y);
      stroke_gpu_active_ = false;
    } else if (gl_enabled_ && gl_renderer_.is_initialized() &&
               brush_.settings().mode != brush::Mode::SMUDGE &&
               brush_.settings().color_rate >= 0.999) {
      makeCurrent();
      stroke_gpu_active_ = true;
      stroke_layer_id_ = document_.active_layer().id();
      stroke_mode_ = brush_.settings().mode;
      stroke_min_x_ = document_.width();
      stroke_min_y_ = document_.height();
      stroke_max_x_ = 0;
      stroke_max_y_ = 0;
      gl_renderer_.clear_stroke();
      std::vector<brush::BrushDab> dabs;
      brush_.begin_dabs(doc_pt, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y, &dabs);
      if (!dabs.empty()) {
        expand_stroke_bbox(dabs);
        gl_renderer_.render_brush_dabs(dabs, document_.width(), document_.height(),
                                       brush_.settings(), brush_.selection());
      }
      doneCurrent();
    } else {
      stroke_gpu_active_ = false;
      brush_.begin(document_.active_layer().image(), doc_pt, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y);
    }
    update();
  }
}

void Canvas::handle_pointer_move(const PointerEvent &ev) {
  const QPointF pos = ev.pos;

  if (pen_gesture_active_) {
    handle_pen_gesture_move(ev);
    return;
  }

  if (rotate_active_) {
    update_rotate(pos.x(), pos.y());
    return;
  }
  if (panning_) {
    pan_by(pos.x() - pan_last_x_, pos.y() - pan_last_y_);
    pan_last_x_ = pos.x();
    pan_last_y_ = pos.y();
    return;
  }

  brush::Point doc_pt;
  map_to_document(pos.x(), pos.y(), doc_pt);

  if (selecting_) {
    doc_pt.x = std::clamp(doc_pt.x, 0.0, static_cast<double>(document_.width()));
    doc_pt.y = std::clamp(doc_pt.y, 0.0, static_cast<double>(document_.height()));
    select_current_ = doc_pt;
    if (tool_ == Tool::LASSO_SELECT) {
      current_lasso_points_.emplace_back(doc_pt.x, doc_pt.y);
    }
    update();
  } else if (moving_) {
    move_current_ = doc_pt;
    if (document_.has_floating_selection()) {
      const auto &fs = document_.floating_selection();
      if (transform_drag_mode_ == TransformDragMode::ROTATE) {
        const double curr_angle = std::atan2(doc_pt.y - (fs.pivot_y + initial_offset_y_),
                                             doc_pt.x - (fs.pivot_x + initial_offset_x_));
        const double start_angle = std::atan2(move_start_.y - (fs.pivot_y + initial_offset_y_),
                                              move_start_.x - (fs.pivot_x + initial_offset_x_));
        double delta_angle = curr_angle - start_angle;
        double new_rot = initial_rotation_ + delta_angle;
        if (ev.modifiers.testFlag(Qt::ShiftModifier)) {
          const double step = std::numbers::pi / 12.0;
          new_rot = std::round(new_rot / step) * step;
        }
        document_.update_transform_selection(initial_offset_x_, initial_offset_y_, new_rot,
                                             initial_scale_x_, initial_scale_y_);
      } else if (transform_drag_mode_ == TransformDragMode::SCALE) {
        const double dist_start = std::hypot(move_start_.x - (fs.pivot_x + initial_offset_x_),
                                             move_start_.y - (fs.pivot_y + initial_offset_y_));
        const double dist_curr = std::hypot(doc_pt.x - (fs.pivot_x + initial_offset_x_),
                                            doc_pt.y - (fs.pivot_y + initial_offset_y_));
        if (dist_start > 1e-3) {
          const double ratio = dist_curr / dist_start;
          double new_sx = std::clamp(initial_scale_x_ * ratio, 0.02, 50.0);
          double new_sy = std::clamp(initial_scale_y_ * ratio, 0.02, 50.0);
          if (ev.modifiers.testFlag(Qt::ShiftModifier)) {
            const double uniform = std::max(new_sx, new_sy);
            new_sx = uniform;
            new_sy = uniform;
          }
          document_.update_transform_selection(initial_offset_x_, initial_offset_y_, initial_rotation_,
                                               new_sx, new_sy);
        }
      } else {
        const int dx = initial_offset_x_ + static_cast<int>(std::round(doc_pt.x - move_start_.x));
        const int dy = initial_offset_y_ + static_cast<int>(std::round(doc_pt.y - move_start_.y));
        document_.update_transform_selection(dx, dy, initial_rotation_,
                                             initial_scale_x_, initial_scale_y_);
      }
    }
    update();
  } else if (stroke_active_) {
    if (selection_layer_mode_) {
      brush_.move_to(selection_image_, doc_pt, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y);
    } else if (stroke_gpu_active_) {
      makeCurrent();
      std::vector<brush::BrushDab> dabs;
      brush_.generate_dabs(doc_pt, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y, dabs);
      if (!dabs.empty()) {
        expand_stroke_bbox(dabs);
        gl_renderer_.render_brush_dabs(dabs, document_.width(), document_.height(),
                                       brush_.settings(), brush_.selection());
      }
      doneCurrent();
    } else {
      brush_.move_to(document_.active_layer().image(), doc_pt, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y);
    }
    last_stroke_input_ = doc_pt;
    const std::uint64_t move_now_ms = core::ProjectTracker::current_timestamp_ms();
    tracker_.append_sample({doc_pt.x, doc_pt.y, ev.sample.pressure, ev.sample.tilt_x, ev.sample.tilt_y, move_now_ms});
    if (move_now_ms >= stroke_start_time_ms_ + 2000 && move_now_ms >= last_timelapse_capture_ms_ + 1000) {
      capture_timelapse_frame(false);
    }
    update();
  }

  if (!panning_ && !space_held_ && tool_ == Tool::MOVE) {
    if (!moving_) {
      update_move_tool_cursor(pos.x(), pos.y());
    } else {
      if (transform_drag_mode_ == TransformDragMode::ROTATE) {
        setCursor(Qt::PointingHandCursor);
      } else if (transform_drag_mode_ == TransformDragMode::SCALE) {
        setCursor(Qt::SizeFDiagCursor);
      } else if (transform_drag_mode_ == TransformDragMode::TRANSLATE) {
        setCursor(Qt::SizeAllCursor);
      }
    }
  }

  // The brush-size ring is painted by draw_overlays, so a plain hover must
  // schedule a repaint for the ring to follow the pointer. selection_layer_mode_
  // is excluded because draw_overlays rebuilds the selection overlay image
  // pixel-by-pixel from every selection tile on each frame.
  if (!panning_ && !selection_layer_mode_) {
    update();
  }

  emit_cursor(pos.x(), pos.y(), ev.sample, stroke_active_);
}

void Canvas::handle_pointer_release(const PointerEvent &ev) {
  const QPointF pos = ev.pos;

  if (pen_gesture_active_) {
    pen_gesture_active_ = false;
    pen_gesture_mode_ = PenGestureMode::NONE;
    stop_rotate();
    return;
  }

  if (rotate_active_) {
    stop_rotate();
    return;
  }
  if (panning_) {
    stop_pan();
    return;
  }

  if (selecting_) {
    core::SelectionCombineMode mode = core::SelectionCombineMode::REPLACE;
    const bool shift = ev.modifiers.testFlag(Qt::ShiftModifier);
    const bool alt = ev.modifiers.testFlag(Qt::AltModifier);
    if (shift && alt) {
      mode = core::SelectionCombineMode::INTERSECT;
    } else if (shift) {
      mode = core::SelectionCombineMode::ADD;
    } else if (alt) {
      mode = core::SelectionCombineMode::SUBTRACT;
    }
    if ((ev.modifiers & ~Qt::KeypadModifier) == Qt::NoModifier) {
      mode = selection_combine_mode_;
    }

    document_.begin_change();
    if (tool_ == Tool::RECT_SELECT) {
      const int rx = static_cast<int>(std::round(std::min(select_start_.x, select_current_.x)));
      const int ry = static_cast<int>(std::round(std::min(select_start_.y, select_current_.y)));
      const int rw = static_cast<int>(std::round(std::abs(select_current_.x - select_start_.x)));
      const int rh = static_cast<int>(std::round(std::abs(select_current_.y - select_start_.y)));
      if (rw > 2 && rh > 2) {
        document_.selection().set_rect(rx, ry, rw, rh, mode);
        brush_.set_selection(&document_.selection());
      } else {
        if (mode == core::SelectionCombineMode::REPLACE) {
          document_.selection().clear();
          brush_.set_selection(nullptr);
          move_auto_selected_ = false;
        }
      }
    } else if (tool_ == Tool::ELLIPSE_SELECT) {
      const int rx = static_cast<int>(std::round(std::min(select_start_.x, select_current_.x)));
      const int ry = static_cast<int>(std::round(std::min(select_start_.y, select_current_.y)));
      const int rw = static_cast<int>(std::round(std::abs(select_current_.x - select_start_.x)));
      const int rh = static_cast<int>(std::round(std::abs(select_current_.y - select_start_.y)));
      if (rw > 2 && rh > 2) {
        document_.selection().set_ellipse(rx, ry, rw, rh, mode);
        brush_.set_selection(&document_.selection());
      } else {
        if (mode == core::SelectionCombineMode::REPLACE) {
          document_.selection().clear();
          brush_.set_selection(nullptr);
          move_auto_selected_ = false;
        }
      }
    } else if (tool_ == Tool::LASSO_SELECT) {
      double min_px = 1e9, max_px = -1e9, min_py = 1e9, max_py = -1e9;
      for (const auto &p : current_lasso_points_) {
        min_px = std::min(min_px, p.first);
        max_px = std::max(max_px, p.first);
        min_py = std::min(min_py, p.second);
        max_py = std::max(max_py, p.second);
      }
      if (current_lasso_points_.size() > 2 && (max_px - min_px > 2.0 || max_py - min_py > 2.0)) {
        document_.selection().add_polygon(current_lasso_points_,
                                          &document_.active_layer().image(),
                                          lasso_mode_ == LassoMode::MAGIC_OBJECT,
                                          mode);
        current_lasso_points_.clear();
        brush_.set_selection(&document_.selection());
      } else {
        current_lasso_points_.clear();
        if (mode == core::SelectionCombineMode::REPLACE) {
          document_.selection().clear();
          brush_.set_selection(nullptr);
          move_auto_selected_ = false;
        }
      }
    }
    document_.commit_change();
    selecting_ = false;
    update();
  } else if (moving_) {
    moving_ = false;
    transform_drag_mode_ = TransformDragMode::NONE;
    if (tool_ == Tool::MOVE) {
      update_move_tool_cursor(pos.x(), pos.y());
    }
    update();
  } else if (stroke_active_) {
    finish_stroke();
  }

  emit_cursor(pos.x(), pos.y(), ev.sample, false);
}

void Canvas::tabletEvent(QTabletEvent *event) {
  const QPointF pos = event->position();
  InputSample sample;
  sample.pressure = std::clamp(event->pressure(), 0.0, 1.0);
  sample.tilt_x = event->xTilt() * std::numbers::pi / 180.0;
  sample.tilt_y = event->yTilt() * std::numbers::pi / 180.0;
  sample.rotation = event->rotation();
  sample.has_pressure = true;
  sample.has_tilt = true;
  sample.has_rotation = (event->rotation() != 0.0);
  sample.is_eraser = (event->pointerType() == QPointingDevice::PointerType::Eraser);

  if (eraser_tip_active_ != sample.is_eraser) {
    eraser_tip_active_ = sample.is_eraser;
    update_brush_mode();
    emit eraserModeChanged(effective_eraser());
  }

  PointerEvent ev{
    .pos = pos,
    .sample = sample,
    .buttons = event->buttons(),
    .button = event->button(),
    .modifiers = event->modifiers()
  };

  switch (event->type()) {
    case QEvent::TabletPress:
      handle_pointer_press(ev);
      break;
    case QEvent::TabletMove:
      handle_pointer_move(ev);
      break;
    case QEvent::TabletRelease:
      handle_pointer_release(ev);
      break;
    default:
      break;
  }
  event->accept();
}

void Canvas::mousePressEvent(QMouseEvent *event) {
  InputSample sample;
  sample.pressure = 1.0;
  sample.has_pressure = false;
  sample.has_tilt = false;
  PointerEvent ev{
    .pos = event->position(),
    .sample = sample,
    .buttons = event->buttons(),
    .button = event->button(),
    .modifiers = event->modifiers()
  };
  handle_pointer_press(ev);
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
  InputSample sample;
  sample.pressure = stroke_active_ ? 1.0 : 0.0;
  sample.has_pressure = false;
  sample.has_tilt = false;
  PointerEvent ev{
    .pos = event->position(),
    .sample = sample,
    .buttons = event->buttons(),
    .button = event->button(),
    .modifiers = event->modifiers()
  };
  handle_pointer_move(ev);
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
  InputSample sample;
  sample.pressure = 0.0;
  sample.has_pressure = false;
  sample.has_tilt = false;
  PointerEvent ev{
    .pos = event->position(),
    .sample = sample,
    .buttons = event->buttons(),
    .button = event->button(),
    .modifiers = event->modifiers()
  };
  handle_pointer_release(ev);
}

void Canvas::wheelEvent(QWheelEvent *event) {
  if (stroke_active_) return;

  const QPoint numPixels = event->pixelDelta();
  const QPoint numDegrees = event->angleDelta() / 8;
  const double dy = !numPixels.isNull() ? numPixels.y() : (numDegrees.y() / 15.0);
  const double dx = !numPixels.isNull() ? numPixels.x() : (numDegrees.x() / 15.0);

  const double ax = cursor_widget_valid_ ? cursor_widget_x_ : width() * 0.5;
  const double ay = cursor_widget_valid_ ? cursor_widget_y_ : height() * 0.5;

  if (event->modifiers() & Qt::ControlModifier) {
    const double factor = (dy > 0) ? kZoomWheelStep : (1.0 / kZoomWheelStep);
    zoom_at(factor, ax, ay);
  } else if (event->modifiers() & Qt::ShiftModifier) {
    pan_by(dy * kPanWheelStep * 0.25, 0);
  } else {
    pan_by(dx * kPanWheelStep * 0.25, dy * kPanWheelStep * 0.25);
  }
  event->accept();
}

bool Canvas::event(QEvent *event) {
  if (event->type() == QEvent::TouchBegin ||
      event->type() == QEvent::TouchUpdate ||
      event->type() == QEvent::TouchEnd ||
      event->type() == QEvent::TouchCancel) {
    handleTouchEvent(static_cast<QTouchEvent *>(event));
    return true;
  }
  return QOpenGLWidget::event(event);
}

namespace {
// Real platform touch points always carry a widget-local position. Synthetic
// points built with QEventPoint(id, state, scenePos, globalPos) leave it unset,
// so fall back to the scene position in that degenerate case only.
QPointF touch_point_position(const QEventPoint &point) {
  const QPointF local = point.position();
  if (local.isNull()) {
    const QPointF scene = point.scenePosition();
    if (!scene.isNull()) return scene;
  }
  return local;
}
} // namespace

void Canvas::handleTouchEvent(QTouchEvent *event) {
  if (stroke_active_) return;

  const auto &points = event->points();
  if (points.size() == 2) {
    const QPointF p1 = touch_point_position(points[0]);
    const QPointF p2 = touch_point_position(points[1]);
    const double dist = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
    const double angle = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());
    const QPointF center = (p1 + p2) * 0.5;

    if (event->type() == QEvent::TouchBegin || !touch_active_) {
      touch_active_ = true;
      last_touch_dist_ = dist;
      last_touch_center_ = center;
      initial_touch_angle_ = angle;
      touch_rotation_start_ = view_rotation_;
      touch_rotation_engaged_ = false;
    } else if (event->type() == QEvent::TouchUpdate) {
      // 1. Pan delta
      const double dx = center.x() - last_touch_center_.x();
      const double dy = center.y() - last_touch_center_.y();
      pan_x_ += dx;
      pan_y_ += dy;
      last_touch_center_ = center;

      // 2. Pinch Zoom centered at current touch point
      if (last_touch_dist_ > 10.0 && dist > 10.0) {
        const double factor = dist / last_touch_dist_;
        const double new_zoom = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
        if (std::abs(new_zoom - zoom_) > 1e-5) {
          update_transform(width(), height());
          const double document_x = (center.x() - view_x_) / view_scale_;
          const double document_y = (center.y() - view_y_) / view_scale_;
          zoom_ = new_zoom;
          update_transform(width(), height());
          pan_x_ += center.x() - (view_x_ + document_x * view_scale_);
          pan_y_ += center.y() - (view_y_ + document_y * view_scale_);
        }
        last_touch_dist_ = dist;
      }

      // 3. Rotation with 8-degree deadband (prevents accidental tilting during pan/zoom)
      double delta_angle = angle - initial_touch_angle_;
      while (delta_angle > std::numbers::pi) delta_angle -= 2.0 * std::numbers::pi;
      while (delta_angle < -std::numbers::pi) delta_angle += 2.0 * std::numbers::pi;

      if (!touch_rotation_engaged_) {
        if (std::abs(delta_angle) > (8.0 * std::numbers::pi / 180.0)) {
          touch_rotation_engaged_ = true;
        }
      }
      if (touch_rotation_engaged_) {
        set_rotation(touch_rotation_start_ + delta_angle);
      }

      update_transform(width(), height());
      update();
    } else if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
      touch_active_ = false;
      touch_rotation_engaged_ = false;
    }
    event->accept();
  } else if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
    touch_active_ = false;
    touch_rotation_engaged_ = false;
  }
}

void Canvas::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    space_held_ = true;
    setCursor(Qt::OpenHandCursor);
  }
  if (event->key() == Qt::Key_Shift) {
    shift_held_ = true;
  }
  if (event->key() == Qt::Key_Q) {
    emit quickMenuRequested(cursor_widget_x(), cursor_widget_y());
  }
  // Enter/Escape and the selection-transform brackets remain canvas-local:
  // they own move_auto_selected_ and must run atomically with the selection
  // state. They are still configurable (see Canvas::selection_key_seq), but
  // dispatched here rather than via ApplicationShortcut to avoid firing during
  // mouse-event sequences.
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    commit_selection_key();
    return;
  }
  if (event->key() == Qt::Key_Escape) {
    cancel_selection_key();
    return;
  }
  // With no selection active, brackets resize the brush.
  if (!document_.has_floating_selection() && !document_.selection().has_selection()) {
    if (event->key() == Qt::Key_BracketRight) {
      const double next = std::clamp(brush_size() * 1.15, 1.0, 500.0);
      set_brush_size(next);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_BracketLeft) {
      const double next = std::clamp(brush_size() / 1.15, 1.0, 500.0);
      set_brush_size(next);
      refresh();
      return;
    }
  }
  if (document_.has_floating_selection() || document_.selection().has_selection()) {
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BraceLeft) {
      const double step = (event->key() == Qt::Key_BraceLeft) ? (std::numbers::pi / 4.0) : (std::numbers::pi / 12.0);
      document_.rotate_selection(-step);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_BracketRight || event->key() == Qt::Key_BraceRight) {
      const double step = (event->key() == Qt::Key_BraceRight) ? (std::numbers::pi / 4.0) : (std::numbers::pi / 12.0);
      document_.rotate_selection(step);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_Minus || event->key() == Qt::Key_Underscore) {
      document_.scale_selection(0.9, 0.9);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
      document_.scale_selection(1.1, 1.1);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
      clear_selection_content_key();
      return;
    }
  }
  if (document_.has_floating_selection()) {
    const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    if (event->key() == Qt::Key_Left) {
      document_.nudge_floating(-step, 0);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_Right) {
      document_.nudge_floating(step, 0);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_Up) {
      document_.nudge_floating(0, -step);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_Down) {
      document_.nudge_floating(0, step);
      refresh();
      return;
    }
    if (event->key() == Qt::Key_H) {
      document_.flip_floating_horizontal();
      refresh();
      return;
    }
    if (event->key() == Qt::Key_V) {
      document_.flip_floating_vertical();
      refresh();
      return;
    }
  }
}

void Canvas::commit_selection_key() {
  if (document_.has_floating_selection()) {
    document_.commit_move_selection();
  }
  if (move_auto_selected_) {
    document_.selection().clear();
    move_auto_selected_ = false;
  }
  refresh();
}

void Canvas::cancel_selection_key() {
  if (document_.has_floating_selection()) {
    document_.cancel_move_selection();
  }
  if (move_auto_selected_) {
    document_.selection().clear();
    move_auto_selected_ = false;
  }
  refresh();
}

void Canvas::clear_selection_content_key() {
  if (document_.has_floating_selection()) {
    document_.cancel_move_selection();
  }
  document_.clear_selection_content();
  document_.begin_change();
  document_.selection().clear();
  document_.commit_change();
  brush_.set_selection(nullptr);
  move_auto_selected_ = false;
  refresh();
}

void Canvas::keyReleaseEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    space_held_ = false;
    if (!panning_) setCursor(Qt::CrossCursor);
  }
  if (event->key() == Qt::Key_Shift) {
    shift_held_ = false;
  }
  QOpenGLWidget::keyReleaseEvent(event);
}

void Canvas::leaveEvent(QEvent *) {
  cursor_inside_ = false;
  update();
}

void Canvas::handle_pen_gesture_move(const PointerEvent &ev) {
  const QPointF pos = ev.pos;

  if (ev.sample.has_rotation) {
    const double twist_diff = ev.sample.rotation - pen_gesture_initial_twist_;
    if (std::abs(twist_diff) > 2.0) {
      pen_gesture_mode_ = PenGestureMode::ROTATE;
      set_rotation(rotate_start_angle_ + twist_diff * (std::numbers::pi / 180.0));
      update();
      return;
    }
  }

  const double dx = pos.x() - pen_gesture_start_.x();
  const double dy = pos.y() - pen_gesture_start_.y();

  if (pen_gesture_mode_ == PenGestureMode::NONE) {
    const double dist = std::hypot(dx, dy);
    if (dist < 4.0) return;

    update_transform(width(), height());
    const double start_ang = std::atan2(rotate_start_y_ - pivot_y_, rotate_start_x_ - pivot_x_);
    const double cur_ang = std::atan2(pos.y() - pivot_y_, pos.x() - pivot_x_);
    double ang_diff = std::abs(cur_ang - start_ang);
    if (ang_diff > std::numbers::pi) ang_diff = 2.0 * std::numbers::pi - ang_diff;

    if (std::abs(dy) > 1.4 * std::abs(dx)) {
      pen_gesture_mode_ = PenGestureMode::ZOOM;
    } else if (ang_diff > (5.0 * std::numbers::pi / 180.0) || std::abs(dx) > 10.0) {
      pen_gesture_mode_ = PenGestureMode::ROTATE;
    } else {
      pen_gesture_mode_ = PenGestureMode::ZOOM;
    }
  }

  if (pen_gesture_mode_ == PenGestureMode::ZOOM) {
    const double step_dy = pen_gesture_last_.y() - pos.y();
    if (std::abs(step_dy) > 0.01) {
      const double factor = std::pow(1.008, step_dy);
      zoom_at(factor, width() * 0.5, height() * 0.5);
    }
    pen_gesture_last_ = pos;
  } else if (pen_gesture_mode_ == PenGestureMode::ROTATE) {
    update_transform(width(), height());
    const double start_ang = std::atan2(rotate_start_y_ - pivot_y_, rotate_start_x_ - pivot_x_);
    const double cur_ang = std::atan2(pos.y() - pivot_y_, pos.x() - pivot_x_);
    set_rotation(rotate_start_angle_ + (cur_ang - start_ang));
    update();
  }
}
void Canvas::capture_timelapse_frame(bool force) {
  const std::uint64_t now_ms = core::ProjectTracker::current_timestamp_ms();
  if (!force && (now_ms - last_timelapse_capture_ms_ < 100)) {
    return;
  }
  last_timelapse_capture_ms_ = now_ms;

  if (gl_enabled_ && gl_renderer_.is_initialized() && isValid()) {
    makeCurrent();
    int out_w = 0, out_h = 0;
    std::vector<core::Rgba8> pixels;
    gl_renderer_.capture_downscaled_composite(1920, pixels, out_w, out_h);
    doneCurrent();
    if (!pixels.empty() && out_w > 0 && out_h > 0) {
      timelapse_recorder_.capture_frame_async(std::move(pixels), out_w, out_h, force);
      return;
    }
  }

  timelapse_recorder_.capture_document_cpu(document_, 1920, force);
}

} // namespace kreart::ui
