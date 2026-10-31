#pragma once

#include "kreart/core/tiled_image.h"
#include "kreart/brush/brush_tip.h"
#include "kreart/brush/sensor_curve.h"
#include "kreart/brush/texture_mask.h"

namespace kreart::core {
class SelectionMask;
}

namespace kreart::brush {

enum class Mode {
  PAINT,
  ERASE,
  SMUDGE,
};
enum class SmudgeMode {
  DIFFUSION,
  SMEAR,
};
enum class SmoothingMode {
  NONE,
  BASIC,
  WEIGHTED,
  STABILIZER,
  DELAYED_STROKE,
};


struct Settings {
  double size = 32.0;
  double opacity = 1.0;
  double flow = 1.0;
  std::string composite_op = "normal";
  double hardness = 0.85;
  double hardness_x = 0.85;
  double hardness_y = 0.85;
  double ratio = 1.0;
  double scatter = 0.0;
  double angle = 0.0;
  bool angle_follow = false;
  double angle_follow_weight = 1.0;
  double angle_random = 0.0;
  // Distance between dabs as a fraction of the brush diameter. Krita presets
  // store this as the Brush "spacing" attribute (0.1 means 10%).
  double spacing = 0.11;
  // Gamma applied to pen pressure before size and flow are derived. Values
  // below 1 are softer at light pressure; values above 1 need more pressure.
  double pressure_curve = 1.0;
  bool tilt_enabled = true;
  double tilt_strength = 0.65;
  Mode mode = Mode::PAINT;
  core::Rgba8 color{24, 24, 28, 255};
  double color_rate = 1.0;
  double smudge_rate = 0.5;
  SmudgeMode smudge_mode = SmudgeMode::DIFFUSION;
  SensorCurve size_curve;
  SensorCurve opacity_curve;
  SensorCurve size_dynamics;
  SensorCurve opacity_dynamics;
  SensorCurve flow_dynamics;
  SensorCurve scatter_dynamics;
  SensorCurve angle_dynamics;
  SensorCurve smudge_dynamics;
  BrushTipPtr tip;
  bool texture_enabled = false;
  TextureMaskPtr texture;
  double texture_scale = 1.0;
  double texture_strength = 0.75;
  double texture_contrast = 1.0;
  double texture_brightness = 0.0;
  bool texture_invert = false;
  double texture_offset_x = 0.0;
  double texture_offset_y = 0.0;
  SmoothingMode smoothing_mode = SmoothingMode::WEIGHTED;
  double smoothing = 0.35;
  double stabilizer_radius = 24.0;
  double smoothing_distance = 15.0;
  bool speed_dynamics = false;
  bool symmetry_vertical = false;
  bool symmetry_horizontal = false;
  double symmetry_axis_x = 0.0;
  double symmetry_axis_y = 0.0;
};

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct BrushDab {
  float x = 0.0f;
  float y = 0.0f;
  float major_radius = 1.0f;
  float minor_radius = 1.0f;
  float angle = 0.0f;
  float inner_radius = 0.0f;
  float flow = 1.0f;
  core::Rgba8 color{0, 0, 0, 255};
};

class RoundBrush {
public:
  void set_settings(const Settings &settings) {
    settings_ = settings;
  }
  void set_selection(const core::SelectionMask *selection) {
    selection_ = selection;
  }
  const core::SelectionMask *selection() const {
    return selection_;
  }

  const Settings &settings() const {
    return settings_;
  }
  const core::TiledImage *stroke_buffer() const {
    return stroke_buffer_.get();
  }

  void begin(core::TiledImage &image, Point point, double pressure, double tilt_x = 0.0,
             double tilt_y = 0.0);
  void jump_to(core::TiledImage &image, Point point, double pressure, double tilt_x = 0.0,
               double tilt_y = 0.0);
  void move_to(core::TiledImage &image, Point point, double pressure, double tilt_x = 0.0,
               double tilt_y = 0.0);
  void end(core::TiledImage *image = nullptr);
  void end(core::TiledImage &image) {
    end(&image);
  }

  void begin_dabs(Point point, double pressure, double tilt_x = 0.0, double tilt_y = 0.0,
                  std::vector<BrushDab> *out_dabs = nullptr);
  void generate_dabs(Point point, double pressure, double tilt_x, double tilt_y,
                     std::vector<BrushDab> &out_dabs);
  void end_dabs(std::vector<BrushDab> &out_dabs);

  bool active() const {
    return active_;
  }
  Point smoothed_point() const {
    return smoothed_point_;
  }
  Point last_dab_point() const {
    return last_dab_point_;
  }


private:
  void stamp(core::TiledImage &image, Point point, double pressure, double tilt_x, double tilt_y,
             double angle = 0.0);
  void stamp_single(core::TiledImage &image, Point point, double pressure, double tilt_x,
                    double tilt_y, double angle = 0.0);
  BrushDab create_dab(Point point, double pressure, double tilt_x, double tilt_y,
                      double angle = 0.0) const;
  void append_dabs(Point point, double pressure, double tilt_x, double tilt_y,
                   std::vector<BrushDab> &out_dabs, double angle = 0.0) const;
  void emit_dab(core::TiledImage *image, std::vector<BrushDab> *out_dabs, Point pt,
                double pr, double tx, double ty, double tangent_angle);
  StrokeInputContext make_context(double pressure, double tilt_x, double tilt_y,
                                  double chord_dist = 0.0) const;

  Settings settings_;
  struct StrokeSample {
    Point point;
    double pressure = 1.0;
    double tilt_x = 0.0;
    double tilt_y = 0.0;
  };

  void interpolate_spline_segment(core::TiledImage *image, std::vector<BrushDab> *out_dabs,
                                  const StrokeSample &p0, const StrokeSample &p1,
                                  const StrokeSample &p2, const StrokeSample &p3,
                                  bool is_tapering = false);

  std::vector<StrokeSample> samples_;
  Point last_dab_point_;
  Point raw_last_point_;
  Point smoothed_point_;
  double smoothed_pressure_ = 1.0;
  double distance_accum_ = 0.0;
  bool active_ = false;
  core::TiledImage *image_ = nullptr;
  std::unique_ptr<core::TiledImage> stroke_buffer_;
  const core::SelectionMask *selection_ = nullptr;
  core::Rgba8 reservoir_color_{0, 0, 0, 0};
  bool has_reservoir_ = false;
  struct StrokeRng {
    std::uint32_t state = 0x853c49e6u;
    void seed(std::uint32_t s) {
      state = (s == 0) ? 0x853c49e6u : s;
    }
    std::uint32_t next_u32() {
      std::uint32_t x = state;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      state = x;
      return x;
    }
    double next_float(double min_val = -1.0, double max_val = 1.0) {
      const double normalized = static_cast<double>(next_u32()) / 4294967295.0;
      return min_val + normalized * (max_val - min_val);
    }
  };

  mutable StrokeRng stroke_rng_;
  std::uint32_t stroke_counter_ = 0;
  double last_tangent_angle_ = 0.0;
  double total_stroke_distance_ = 0.0;
  Point velocity_{0.0, 0.0};
  double reservoir_wetness_ = 1.0;
};

} // namespace kreart::brush
