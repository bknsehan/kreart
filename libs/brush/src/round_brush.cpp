#include "kreart/brush/round_brush.h"
#include "kreart/core/selection_mask.h"

#include <algorithm>
#include <cmath>

namespace kreart::brush {
namespace {
void blend_brush_pixel(core::Rgba8 &dst, core::Rgba8 src, double stroke_opacity,
                       const std::string &blend_mode) {
  const double effective_sa = (src.a / 255.0) * stroke_opacity;
  if (effective_sa <= 0.0001) {
    return;
  }

  if (dst.a == 0) {
    dst = core::Rgba8{src.r, src.g, src.b, static_cast<std::uint8_t>(std::clamp(std::lround(effective_sa * 255.0), 0L, 255L))};
    return;
  }

  const double da = dst.a / 255.0;
  const double out_a = effective_sa + da * (1.0 - effective_sa);
  if (out_a <= 0.0001) {
    dst = core::Rgba8{0, 0, 0, 0};
    return;
  }

  const double sr = src.r / 255.0;
  const double sg = src.g / 255.0;
  const double sb = src.b / 255.0;
  const double dr = dst.r / 255.0;
  const double dg = dst.g / 255.0;
  const double db = dst.b / 255.0;

  double br = sr, bg = sg, bb = sb;

  if (blend_mode == "multiply") {
    br = sr * dr;
    bg = sg * dg;
    bb = sb * db;
  } else if (blend_mode == "screen") {
    br = 1.0 - (1.0 - sr) * (1.0 - dr);
    bg = 1.0 - (1.0 - sg) * (1.0 - dg);
    bb = 1.0 - (1.0 - sb) * (1.0 - db);
  } else if (blend_mode == "add" || blend_mode == "linear_dodge") {
    br = std::min(1.0, sr + dr);
    bg = std::min(1.0, sg + dg);
    bb = std::min(1.0, sb + db);
  } else if (blend_mode == "overlay") {
    auto overlay_ch = [](double s, double d) {
      return (d < 0.5) ? (2.0 * s * d) : (1.0 - 2.0 * (1.0 - s) * (1.0 - d));
    };
    br = overlay_ch(sr, dr);
    bg = overlay_ch(sg, dg);
    bb = overlay_ch(sb, db);
  } else if (blend_mode == "soft_light") {
    auto soft_ch = [](double s, double d) {
      return (1.0 - 2.0 * s) * d * d + 2.0 * s * d;
    };
    br = soft_ch(sr, dr);
    bg = soft_ch(sg, dg);
    bb = soft_ch(sb, db);
  } else if (blend_mode == "darken") {
    br = std::min(sr, dr);
    bg = std::min(sg, dg);
    bb = std::min(sb, db);
  } else if (blend_mode == "lighten") {
    br = std::max(sr, dr);
    bg = std::max(sg, dg);
    bb = std::max(sb, db);
  } else if (blend_mode == "color_burn") {
    auto burn_ch = [](double s, double d) {
      return (s <= 0.0) ? 0.0 : std::max(0.0, 1.0 - (1.0 - d) / s);
    };
    br = burn_ch(sr, dr);
    bg = burn_ch(sg, dg);
    bb = burn_ch(sb, db);
  } else if (blend_mode == "color_dodge") {
    auto dodge_ch = [](double s, double d) {
      return (s >= 1.0) ? 1.0 : std::min(1.0, d / (1.0 - s));
    };
    br = dodge_ch(sr, dr);
    bg = dodge_ch(sg, dg);
    bb = dodge_ch(sb, db);
  }

  const double out_r = (br * effective_sa + dr * da * (1.0 - effective_sa)) / out_a;
  const double out_g = (bg * effective_sa + dg * da * (1.0 - effective_sa)) / out_a;
  const double out_b = (bb * effective_sa + db * da * (1.0 - effective_sa)) / out_a;

  dst.r = static_cast<std::uint8_t>(std::clamp(std::lround(out_r * 255.0), 0L, 255L));
  dst.g = static_cast<std::uint8_t>(std::clamp(std::lround(out_g * 255.0), 0L, 255L));
  dst.b = static_cast<std::uint8_t>(std::clamp(std::lround(out_b * 255.0), 0L, 255L));
  dst.a = static_cast<std::uint8_t>(std::clamp(std::lround(out_a * 255.0), 0L, 255L));
}
} // namespace

void RoundBrush::begin(core::TiledImage &image, Point point, double pressure, double tilt_x,
                       double tilt_y) {
  active_ = true;
  image_ = &image;
  if (settings_.mode != Mode::SMUDGE) {
    stroke_buffer_ = std::make_unique<core::TiledImage>(image.width(), image.height());
  } else {
    stroke_buffer_.reset();
  }
  total_stroke_distance_ = 0.0;
  samples_.clear();
  distance_accum_ = 0.0;
  smoothed_point_ = point;
  smoothed_pressure_ = std::clamp(pressure, 0.0, 1.0);
  raw_last_point_ = point;
  last_dab_point_ = point;
  has_reservoir_ = false;
  reservoir_wetness_ = 1.0;
  reservoir_color_ = settings_.color;
  stroke_rng_.seed(0x1337c0deu + (++stroke_counter_ * 7919u));
  last_tangent_angle_ = 0.0;
  velocity_ = {0.0, 0.0};

  const StrokeSample s{point, smoothed_pressure_, std::clamp(tilt_x, -1.0, 1.0),
                       std::clamp(tilt_y, -1.0, 1.0)};
  samples_.push_back(s);
  emit_dab(&image, nullptr, s.point, s.pressure, s.tilt_x, s.tilt_y, 0.0);
}

void RoundBrush::jump_to(core::TiledImage &image, Point point, double pressure, double tilt_x,
                         double tilt_y) {
  if (!active_) {
    return;
  }
  image_ = &image;
  samples_.clear();
  distance_accum_ = 0.0;
  smoothed_point_ = point;
  smoothed_pressure_ = std::clamp(pressure, 0.0, 1.0);
  raw_last_point_ = point;
  last_dab_point_ = point;
  has_reservoir_ = false;
  reservoir_color_ = settings_.color;
  last_tangent_angle_ = 0.0;

  const StrokeSample s{point, smoothed_pressure_, std::clamp(tilt_x, -1.0, 1.0),
                       std::clamp(tilt_y, -1.0, 1.0)};
  samples_.push_back(s);
  emit_dab(&image, nullptr, s.point, s.pressure, s.tilt_x, s.tilt_y, 0.0);
}

void RoundBrush::move_to(core::TiledImage &image, Point point, double pressure, double tilt_x,
                         double tilt_y) {
  if (!active_) {
    return;
  }
  image_ = &image;
  raw_last_point_ = point;
  if (samples_.empty()) {
    begin(image, point, pressure, tilt_x, tilt_y);
    return;
  }

  if (settings_.smoothing_mode == SmoothingMode::NONE) {
    smoothed_point_ = point;
    smoothed_pressure_ = std::clamp(pressure, 0.0, 1.0);
    const auto &prev = samples_.back();
    const double dx = point.x - prev.point.x;
    const double dy = point.y - prev.point.y;
    const double dist = std::hypot(dx, dy);
    if (dist > 1e-5) {
      last_tangent_angle_ = std::atan2(dy, dx);
    }
    const double spacing = std::max(1.2, settings_.size * std::max(0.035, std::clamp(settings_.spacing, 0.01, 2.0)));
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / spacing)));
    for (int i = 1; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      const Point interp_pt{prev.point.x + t * (point.x - prev.point.x),
                            prev.point.y + t * (point.y - prev.point.y)};
      const double interp_p = prev.pressure + t * (smoothed_pressure_ - prev.pressure);
      const double interp_tx = prev.tilt_x + t * (tilt_x - prev.tilt_x);
      const double interp_ty = prev.tilt_y + t * (tilt_y - prev.tilt_y);
      emit_dab(&image, nullptr, interp_pt, interp_p, interp_tx, interp_ty, last_tangent_angle_);
      last_dab_point_ = interp_pt;
    }
    samples_.clear();
    samples_.push_back(StrokeSample{point, smoothed_pressure_,
                                    std::clamp(tilt_x, -1.0, 1.0), std::clamp(tilt_y, -1.0, 1.0)});
    return;
  }

  Point target_pt = point;
  if (settings_.smoothing_mode == SmoothingMode::BASIC) {
    smoothed_point_ = point;
    target_pt = point;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += 0.5 * (raw_p - smoothed_pressure_);
  } else if (settings_.smoothing_mode == SmoothingMode::WEIGHTED) {
    const double smooth_weight = std::clamp(settings_.smoothing, 0.0, 0.95);
    const double alpha = 1.0 - smooth_weight * 0.72;
    smoothed_point_.x += (point.x - smoothed_point_.x) * alpha;
    smoothed_point_.y += (point.y - smoothed_point_.y) * alpha;
    target_pt = smoothed_point_;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += 0.45 * (raw_p - smoothed_pressure_);
  } else if (settings_.smoothing_mode == SmoothingMode::STABILIZER) {
    const double leash = std::clamp(settings_.stabilizer_radius, 4.0, 120.0);
    const double dx = point.x - smoothed_point_.x;
    const double dy = point.y - smoothed_point_.y;
    const double d = std::hypot(dx, dy);
    if (d > leash) {
      const double excess = d - leash;
      smoothed_point_.x += (dx / d) * excess;
      smoothed_point_.y += (dy / d) * excess;
    }
    target_pt = smoothed_point_;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += 0.35 * (raw_p - smoothed_pressure_);
  } else if (settings_.smoothing_mode == SmoothingMode::DELAYED_STROKE) {
    const double mass = std::clamp(settings_.smoothing_distance, 2.0, 100.0);
    const double drag = 1.0 / (mass * 0.5);
    const double fx = point.x - smoothed_point_.x;
    const double fy = point.y - smoothed_point_.y;
    velocity_.x = (velocity_.x + fx / mass) * (1.0 - drag * 0.5);
    velocity_.y = (velocity_.y + fy / mass) * (1.0 - drag * 0.5);
    smoothed_point_.x += velocity_.x;
    smoothed_point_.y += velocity_.y;
    target_pt = smoothed_point_;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += std::clamp(drag * 0.8, 0.05, 0.5) * (raw_p - smoothed_pressure_);
  }
  const StrokeSample current{target_pt, smoothed_pressure_,
                             std::clamp(tilt_x, -1.0, 1.0), std::clamp(tilt_y, -1.0, 1.0)};
  samples_.push_back(current);
  // 3. C1 lookahead Catmull-Rom spline
  if (samples_.size() == 3) {
    const auto &p1 = samples_[0];
    const auto &p2 = samples_[1];
    const auto &p3 = samples_[2];
    const StrokeSample p0{Point{2.0 * p1.point.x - p2.point.x, 2.0 * p1.point.y - p2.point.y},
                          p1.pressure, p1.tilt_x, p1.tilt_y};
    interpolate_spline_segment(&image, nullptr, p0, p1, p2, p3, false);
  } else if (samples_.size() >= 4) {
    const auto &p0 = samples_[samples_.size() - 4];
    const auto &p1 = samples_[samples_.size() - 3];
    const auto &p2 = samples_[samples_.size() - 2];
    const auto &p3 = samples_[samples_.size() - 1];
    interpolate_spline_segment(&image, nullptr, p0, p1, p2, p3, false);

    if (samples_.size() > 5) {
      samples_.erase(samples_.begin(), samples_.begin() + (samples_.size() - 4));
    }
  }
}

void RoundBrush::interpolate_spline_segment(core::TiledImage *image,
                                            std::vector<BrushDab> *out_dabs,
                                            const StrokeSample &p0, const StrokeSample &p1,
                                            const StrokeSample &p2, const StrokeSample &p3,
                                            bool is_tapering) {
  const double spacing = std::max(1.2, settings_.size * std::max(0.035, std::clamp(settings_.spacing, 0.01, 2.0)));
  const double chord_dist = std::hypot(p2.point.x - p1.point.x, p2.point.y - p1.point.y);
  if (chord_dist < 0.25) {
    return;
  }

  const int steps = std::max(8, static_cast<int>(std::ceil(chord_dist * 2.5)));
  Point prev_pt = p1.point;

  for (int step = 1; step <= steps; ++step) {
    const double t = static_cast<double>(step) / steps;
    const double t2 = t * t;
    const double t3 = t2 * t;

    auto eval_coord = [t, t2, t3](double v0, double v1, double v2, double v3) -> double {
      return 0.5 * ((2.0 * v1) +
                    (-v0 + v2) * t +
                    (2.0 * v0 - 5.0 * v1 + 4.0 * v2 - v3) * t2 +
                    (-v0 + 3.0 * v1 - 3.0 * v2 + v3) * t3);
    };

    const Point pt{eval_coord(p0.point.x, p1.point.x, p2.point.x, p3.point.x),
                   eval_coord(p0.point.y, p1.point.y, p2.point.y, p3.point.y)};
    double pr = eval_coord(p0.pressure, p1.pressure, p2.pressure, p3.pressure);
    if (settings_.speed_dynamics && chord_dist > 5.0) {
      const double speed_taper = 1.0 - 0.25 * std::clamp(chord_dist / 60.0, 0.0, 1.0);
      pr *= speed_taper;
    }
    if (is_tapering) {
      pr *= std::max(0.0, 1.0 - t * 0.85);
    }
    const double tx = eval_coord(p0.tilt_x, p1.tilt_x, p2.tilt_x, p3.tilt_x);
    const double ty = eval_coord(p0.tilt_y, p1.tilt_y, p2.tilt_y, p3.tilt_y);

    const double sub_dx = pt.x - prev_pt.x;
    const double sub_dy = pt.y - prev_pt.y;
    const double sub_dist = std::hypot(sub_dx, sub_dy);
    if (sub_dist > 1e-5) {
      last_tangent_angle_ = std::atan2(sub_dy, sub_dx);
    }
    distance_accum_ += sub_dist;
    prev_pt = pt;
    total_stroke_distance_ += sub_dist;

    while (distance_accum_ >= spacing) {
      distance_accum_ -= spacing;
      emit_dab(image, out_dabs, pt, pr, tx, ty, last_tangent_angle_);
      last_dab_point_ = pt;
    }
  }
}

void RoundBrush::end(core::TiledImage *image) {
  core::TiledImage *target = image ? image : image_;
  if (active_ && target && !samples_.empty()) {
    if (settings_.smoothing_mode == SmoothingMode::DELAYED_STROKE) {
      for (int step = 0; step < 20; ++step) {
        const double dist_to_end = std::hypot(raw_last_point_.x - smoothed_point_.x,
                                              raw_last_point_.y - smoothed_point_.y);
        if (dist_to_end <= 1.0) break;
        const double mass = std::clamp(settings_.smoothing_distance, 2.0, 100.0);
        const double drag = 1.0 / (mass * 0.5);
        velocity_.x = (velocity_.x + (raw_last_point_.x - smoothed_point_.x) / mass) * (1.0 - drag * 0.5);
        velocity_.y = (velocity_.y + (raw_last_point_.y - smoothed_point_.y) / mass) * (1.0 - drag * 0.5);
        smoothed_point_.x += velocity_.x;
        smoothed_point_.y += velocity_.y;
        samples_.push_back(StrokeSample{smoothed_point_, smoothed_pressure_,
                                        samples_.back().tilt_x, samples_.back().tilt_y});
      }
    }
    if (samples_.size() == 2) {
      const auto &p1 = samples_[0];
      const auto &p2 = samples_[1];
      const StrokeSample p0{Point{2.0 * p1.point.x - p2.point.x, 2.0 * p1.point.y - p2.point.y},
                            p1.pressure, p1.tilt_x, p1.tilt_y};
      const StrokeSample p3{Point{2.0 * p2.point.x - p1.point.x, 2.0 * p2.point.y - p1.point.y},
                            p2.pressure, p2.tilt_x, p2.tilt_y};
      interpolate_spline_segment(target, nullptr, p0, p1, p2, p3, false);
    } else if (samples_.size() >= 3) {
      const auto &p0 = samples_[samples_.size() - 3];
      const auto &p1 = samples_[samples_.size() - 2];
      const auto &p2 = samples_[samples_.size() - 1];
      const StrokeSample p3{Point{2.0 * p2.point.x - p1.point.x, 2.0 * p2.point.y - p1.point.y},
                            p2.pressure, p2.tilt_x, p2.tilt_y};
      interpolate_spline_segment(target, nullptr, p0, p1, p2, p3, false);
    }

    // Final stroke release taper to exact release coordinate
    if (std::hypot(raw_last_point_.x - samples_.back().point.x,
                   raw_last_point_.y - samples_.back().point.y) > 0.5) {
      const auto &p1 = samples_.back();
      const StrokeSample p2{raw_last_point_, p1.pressure * 0.25, p1.tilt_x, p1.tilt_y};
      const auto p0 = (samples_.size() >= 2) ? samples_[samples_.size() - 2] : p1;
      const StrokeSample p3{Point{2.0 * p2.point.x - p1.point.x, 2.0 * p2.point.y - p1.point.y},
                            0.0, p2.tilt_x, p2.tilt_y};
      interpolate_spline_segment(target, nullptr, p0, p1, p2, p3, true);
    }
  }

  // Bake stroke_buffer_ onto target
  if (stroke_buffer_ && target && settings_.mode != Mode::SMUDGE) {
    const double stroke_opacity = std::clamp(settings_.opacity, 0.0, 1.0);
    const bool alpha_locked = target->alpha_locked();

    for (const auto &[key, tile_ptr] : stroke_buffer_->tiles()) {
      const int tile_x = core::TiledImage::key_x(key);
      const int tile_y = core::TiledImage::key_y(key);
      for (int ly = 0; ly < core::kTileSize; ++ly) {
        const int gy = tile_y * core::kTileSize + ly;
        if (gy >= target->height()) continue;
        for (int lx = 0; lx < core::kTileSize; ++lx) {
          const int gx = tile_x * core::kTileSize + lx;
          if (gx >= target->width()) continue;

          const auto buf_px = tile_ptr->pixels[static_cast<std::size_t>(ly) * core::kTileSize + lx];
          if (buf_px.a == 0) continue;

          if (settings_.mode == Mode::ERASE) {
            const std::uint8_t erase_amt = static_cast<std::uint8_t>(
                std::clamp(std::lround((buf_px.a / 255.0) * stroke_opacity * 255.0), 0L, 255L));
            target->erase_pixel(gx, gy, erase_amt);
          } else {
            if (alpha_locked && target->pixel(gx, gy).a == 0) {
              continue;
            }
            core::Rgba8 dst_px = target->pixel(gx, gy);
            blend_brush_pixel(dst_px, buf_px, stroke_opacity, settings_.composite_op);
            target->set_pixel(gx, gy, dst_px);
          }
        }
      }
    }
    stroke_buffer_.reset();
  }

  samples_.clear();
  distance_accum_ = 0.0;
  active_ = false;
  image_ = nullptr;
}

void RoundBrush::begin_dabs(Point point, double pressure, double tilt_x, double tilt_y,
                            std::vector<BrushDab> *out_dabs) {
  active_ = true;
  image_ = nullptr;
  samples_.clear();
  distance_accum_ = 0.0;
  smoothed_point_ = point;
  smoothed_pressure_ = std::clamp(pressure, 0.0, 1.0);
  raw_last_point_ = point;
  last_dab_point_ = point;
  has_reservoir_ = false;
  reservoir_wetness_ = 1.0;
  reservoir_color_ = settings_.color;
  stroke_rng_.seed(0x1337c0deu + (++stroke_counter_ * 7919u));
  last_tangent_angle_ = 0.0;
  velocity_ = {0.0, 0.0};

  const StrokeSample s{point, smoothed_pressure_, std::clamp(tilt_x, -1.0, 1.0),
                       std::clamp(tilt_y, -1.0, 1.0)};
  samples_.push_back(s);
  if (out_dabs) {
    emit_dab(nullptr, out_dabs, s.point, s.pressure, s.tilt_x, s.tilt_y, 0.0);
  }
}

void RoundBrush::generate_dabs(Point point, double pressure, double tilt_x, double tilt_y,
                               std::vector<BrushDab> &out_dabs) {
  if (!active_ || samples_.empty()) {
    begin_dabs(point, pressure, tilt_x, tilt_y, &out_dabs);
    return;
  }
  raw_last_point_ = point;

  if (settings_.smoothing_mode == SmoothingMode::NONE) {
    smoothed_point_ = point;
    smoothed_pressure_ = std::clamp(pressure, 0.0, 1.0);
    const auto &prev = samples_.back();
    const double dx = point.x - prev.point.x;
    const double dy = point.y - prev.point.y;
    const double dist = std::hypot(dx, dy);
    if (dist > 1e-5) {
      last_tangent_angle_ = std::atan2(dy, dx);
    }
    const double spacing = std::max(1.2, settings_.size * std::max(0.035, std::clamp(settings_.spacing, 0.01, 2.0)));
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / spacing)));
    for (int i = 1; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      const Point interp_pt{prev.point.x + t * (point.x - prev.point.x),
                            prev.point.y + t * (point.y - prev.point.y)};
      const double interp_p = prev.pressure + t * (smoothed_pressure_ - prev.pressure);
      const double interp_tx = prev.tilt_x + t * (tilt_x - prev.tilt_x);
      const double interp_ty = prev.tilt_y + t * (tilt_y - prev.tilt_y);
      emit_dab(nullptr, &out_dabs, interp_pt, interp_p, interp_tx, interp_ty, last_tangent_angle_);
      last_dab_point_ = interp_pt;
    }
    samples_.clear();
    samples_.push_back(StrokeSample{point, smoothed_pressure_,
                                    std::clamp(tilt_x, -1.0, 1.0), std::clamp(tilt_y, -1.0, 1.0)});
    return;
  }
  Point target_pt = point;
  if (settings_.smoothing_mode == SmoothingMode::BASIC) {
    smoothed_point_ = point;
    target_pt = point;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += 0.5 * (raw_p - smoothed_pressure_);
  } else if (settings_.smoothing_mode == SmoothingMode::WEIGHTED) {
    const double smooth_weight = std::clamp(settings_.smoothing, 0.0, 0.95);
    const double alpha = 1.0 - smooth_weight * 0.72;
    smoothed_point_.x += (point.x - smoothed_point_.x) * alpha;
    smoothed_point_.y += (point.y - smoothed_point_.y) * alpha;
    target_pt = smoothed_point_;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += 0.45 * (raw_p - smoothed_pressure_);
  } else if (settings_.smoothing_mode == SmoothingMode::STABILIZER) {
    const double leash = std::clamp(settings_.stabilizer_radius, 4.0, 120.0);
    const double dx = point.x - smoothed_point_.x;
    const double dy = point.y - smoothed_point_.y;
    const double d = std::hypot(dx, dy);
    if (d > leash) {
      const double excess = d - leash;
      smoothed_point_.x += (dx / d) * excess;
      smoothed_point_.y += (dy / d) * excess;
    }
    target_pt = smoothed_point_;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += 0.35 * (raw_p - smoothed_pressure_);
  } else if (settings_.smoothing_mode == SmoothingMode::DELAYED_STROKE) {
    const double mass = std::clamp(settings_.smoothing_distance, 2.0, 100.0);
    const double drag = 1.0 / (mass * 0.5);
    const double fx = point.x - smoothed_point_.x;
    const double fy = point.y - smoothed_point_.y;
    velocity_.x = (velocity_.x + fx / mass) * (1.0 - drag * 0.5);
    velocity_.y = (velocity_.y + fy / mass) * (1.0 - drag * 0.5);
    smoothed_point_.x += velocity_.x;
    smoothed_point_.y += velocity_.y;
    target_pt = smoothed_point_;
    const double raw_p = std::clamp(pressure, 0.0, 1.0);
    smoothed_pressure_ += std::clamp(drag * 0.8, 0.05, 0.5) * (raw_p - smoothed_pressure_);
  }
  const StrokeSample current{target_pt, smoothed_pressure_,
                             std::clamp(tilt_x, -1.0, 1.0), std::clamp(tilt_y, -1.0, 1.0)};
  samples_.push_back(current);

  if (samples_.size() == 3) {
    const auto &p1 = samples_[0];
    const auto &p2 = samples_[1];
    const auto &p3 = samples_[2];
    const StrokeSample p0{Point{2.0 * p1.point.x - p2.point.x, 2.0 * p1.point.y - p2.point.y},
                          p1.pressure, p1.tilt_x, p1.tilt_y};
    interpolate_spline_segment(nullptr, &out_dabs, p0, p1, p2, p3, false);
  } else if (samples_.size() >= 4) {
    const auto &p0 = samples_[samples_.size() - 4];
    const auto &p1 = samples_[samples_.size() - 3];
    const auto &p2 = samples_[samples_.size() - 2];
    const auto &p3 = samples_[samples_.size() - 1];
    interpolate_spline_segment(nullptr, &out_dabs, p0, p1, p2, p3, false);

    if (samples_.size() > 5) {
      samples_.erase(samples_.begin(), samples_.begin() + (samples_.size() - 4));
    }
  }
}

void RoundBrush::end_dabs(std::vector<BrushDab> &out_dabs) {
  if (active_ && !samples_.empty()) {
    if (settings_.smoothing_mode == SmoothingMode::DELAYED_STROKE) {
      for (int step = 0; step < 20; ++step) {
        const double dist_to_end = std::hypot(raw_last_point_.x - smoothed_point_.x,
                                              raw_last_point_.y - smoothed_point_.y);
        if (dist_to_end <= 1.0) break;
        const double mass = std::clamp(settings_.smoothing_distance, 2.0, 100.0);
        const double drag = 1.0 / (mass * 0.5);
        velocity_.x = (velocity_.x + (raw_last_point_.x - smoothed_point_.x) / mass) * (1.0 - drag * 0.5);
        velocity_.y = (velocity_.y + (raw_last_point_.y - smoothed_point_.y) / mass) * (1.0 - drag * 0.5);
        smoothed_point_.x += velocity_.x;
        smoothed_point_.y += velocity_.y;
        samples_.push_back(StrokeSample{smoothed_point_, smoothed_pressure_,
                                        samples_.back().tilt_x, samples_.back().tilt_y});
      }
    }
    if (samples_.size() == 2) {
      const auto &p1 = samples_[0];
      const auto &p2 = samples_[1];
      const StrokeSample p0{Point{2.0 * p1.point.x - p2.point.x, 2.0 * p1.point.y - p2.point.y},
                            p1.pressure, p1.tilt_x, p1.tilt_y};
      const StrokeSample p3{Point{2.0 * p2.point.x - p1.point.x, 2.0 * p2.point.y - p1.point.y},
                            p2.pressure, p2.tilt_x, p2.tilt_y};
      interpolate_spline_segment(nullptr, &out_dabs, p0, p1, p2, p3, false);
    } else if (samples_.size() >= 3) {
      const auto &p0 = samples_[samples_.size() - 3];
      const auto &p1 = samples_[samples_.size() - 2];
      const auto &p2 = samples_[samples_.size() - 1];
      const StrokeSample p3{Point{2.0 * p2.point.x - p1.point.x, 2.0 * p2.point.y - p1.point.y},
                            p2.pressure, p2.tilt_x, p2.tilt_y};
      interpolate_spline_segment(nullptr, &out_dabs, p0, p1, p2, p3, false);
    }

    if (std::hypot(raw_last_point_.x - samples_.back().point.x,
                   raw_last_point_.y - samples_.back().point.y) > 0.5) {
      const auto &p1 = samples_.back();
      const StrokeSample p2{raw_last_point_, p1.pressure * 0.25, p1.tilt_x, p1.tilt_y};
      const auto p0 = (samples_.size() >= 2) ? samples_[samples_.size() - 2] : p1;
      const StrokeSample p3{Point{2.0 * p2.point.x - p1.point.x, 2.0 * p2.point.y - p1.point.y},
                            0.0, p2.tilt_x, p2.tilt_y};
      interpolate_spline_segment(nullptr, &out_dabs, p0, p1, p2, p3, true);
    }
  }
  samples_.clear();
  distance_accum_ = 0.0;
  active_ = false;
  image_ = nullptr;
}

StrokeInputContext RoundBrush::make_context(double pressure, double tilt_x, double tilt_y,
                                            double chord_dist) const {
  StrokeInputContext ctx;
  ctx.pressure = std::clamp(pressure, 0.0, 1.0);
  ctx.tilt = std::min(1.0, std::hypot(std::clamp(tilt_x, -1.0, 1.0), std::clamp(tilt_y, -1.0, 1.0)));
  ctx.speed = std::clamp(chord_dist / 60.0, 0.0, 1.0);
  ctx.distance = std::clamp(total_stroke_distance_ / 1000.0, 0.0, 1.0);
  ctx.fuzzy = stroke_rng_.next_float(0.0, 1.0);
  return ctx;
}

void RoundBrush::emit_dab(core::TiledImage *image, std::vector<BrushDab> *out_dabs, Point pt,
                          double pr, double tx, double ty, double tangent_angle) {
  const StrokeInputContext ctx = make_context(pr, tx, ty);
  double dab_angle = settings_.angle;
  if (settings_.angle_dynamics.active()) {
    dab_angle += settings_.angle_dynamics.evaluate_context(ctx) * 2.0 * M_PI;
  } else {
    if (settings_.angle_follow) {
      dab_angle += tangent_angle * settings_.angle_follow_weight;
    }
    if (settings_.angle_random > 0.0) {
      dab_angle += stroke_rng_.next_float(-1.0, 1.0) * settings_.angle_random;
    }
  }

  Point dab_pt = pt;
  double sc_amount = settings_.scatter;
  if (settings_.scatter_dynamics.active()) {
    sc_amount = std::max(0.0, settings_.scatter_dynamics.evaluate_context(ctx));
  }
  if (sc_amount > 0.0) {
    const double pressure_curve = std::clamp(settings_.pressure_curve, 0.1, 8.0);
    const double shaped_p = settings_.size_dynamics.active()
                                ? settings_.size_dynamics.evaluate_context(ctx)
                                : (settings_.size_curve.active()
                                       ? settings_.size_curve.evaluate(ctx.pressure)
                                       : std::pow(ctx.pressure, pressure_curve));
    const double cur_radius = std::max(0.35, settings_.size * 0.5 * (0.15 + 0.85 * shaped_p));
    const double offset_dist = cur_radius * sc_amount * stroke_rng_.next_float(-1.0, 1.0);
    dab_pt.x += -std::sin(tangent_angle) * offset_dist;
    dab_pt.y += std::cos(tangent_angle) * offset_dist;
  }

  if (image) {
    core::TiledImage &dest_img = (stroke_buffer_ && settings_.mode != Mode::SMUDGE)
                                     ? *stroke_buffer_
                                     : *image;
    stamp(dest_img, dab_pt, std::clamp(pr, 0.0, 1.0), std::clamp(tx, -1.0, 1.0),
          std::clamp(ty, -1.0, 1.0), dab_angle);
  }
  if (out_dabs) {
    append_dabs(dab_pt, std::clamp(pr, 0.0, 1.0), std::clamp(tx, -1.0, 1.0),
                std::clamp(ty, -1.0, 1.0), *out_dabs, dab_angle);
  }
}

BrushDab RoundBrush::create_dab(Point point, double pressure, double tilt_x, double tilt_y,
                                double angle) const {
  const StrokeInputContext ctx = make_context(pressure, tilt_x, tilt_y);
  const double pressure_curve = std::clamp(settings_.pressure_curve, 0.1, 8.0);
  const double shaped_size = settings_.size_dynamics.active()
                                 ? settings_.size_dynamics.evaluate_context(ctx)
                                 : (settings_.size_curve.active()
                                        ? settings_.size_curve.evaluate(ctx.pressure)
                                        : std::pow(ctx.pressure, pressure_curve));
  const double radius = std::max(0.35, settings_.size * 0.5 * (0.15 + 0.85 * shaped_size));
  const double base_major = radius;
  const double ratio = std::clamp(settings_.ratio, 0.05, 20.0);
  const double base_minor = radius * ratio;

  const double clamped_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const double clamped_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  const double tilt_magnitude = std::min(1.0, std::hypot(clamped_tilt_x, clamped_tilt_y));
  const double tilt_amount =
      settings_.tilt_enabled ? std::clamp(settings_.tilt_strength, 0.0, 1.0) * tilt_magnitude : 0.0;
  const double major_radius = base_major * (1.0 + 0.8 * tilt_amount);
  const double minor_radius = base_minor * std::max(0.2, 1.0 - 0.55 * tilt_amount);

  const double avg_hardness = std::clamp(0.5 * (settings_.hardness_x + settings_.hardness_y), 0.0, 1.0);
  const double inner = std::max(0.0, major_radius * avg_hardness - 0.75);
  const double flow_response = settings_.flow_dynamics.active()
                                   ? settings_.flow_dynamics.evaluate_context(ctx)
                                   : (settings_.opacity_curve.active()
                                          ? settings_.opacity_curve.evaluate(ctx.pressure)
                                          : (0.35 + 0.65 * shaped_size));
  const double flow = std::clamp(settings_.flow, 0.0, 1.0) * flow_response;
  const double tilt_angle = (tilt_magnitude > 0.001) ? std::atan2(clamped_tilt_y, clamped_tilt_x) : 0.0;
  const double final_angle = angle + (tilt_amount > 0.001 ? tilt_angle : 0.0);

  BrushDab dab;
  dab.x = static_cast<float>(point.x);
  dab.y = static_cast<float>(point.y);
  dab.major_radius = static_cast<float>(major_radius);
  dab.minor_radius = static_cast<float>(minor_radius);
  dab.angle = static_cast<float>(final_angle);
  dab.inner_radius = static_cast<float>(inner);
  dab.flow = static_cast<float>(flow);
  dab.color = settings_.color;
  return dab;
}

void RoundBrush::append_dabs(Point point, double pressure, double tilt_x, double tilt_y,
                             std::vector<BrushDab> &out_dabs, double angle) const {
  out_dabs.push_back(create_dab(point, pressure, tilt_x, tilt_y, angle));
  if (settings_.symmetry_vertical && !settings_.symmetry_horizontal) {
    const Point mirror_pt{2.0 * settings_.symmetry_axis_x - point.x, point.y};
    out_dabs.push_back(create_dab(mirror_pt, pressure, -tilt_x, tilt_y, -angle));
  } else if (!settings_.symmetry_vertical && settings_.symmetry_horizontal) {
    const Point mirror_pt{point.x, 2.0 * settings_.symmetry_axis_y - point.y};
    out_dabs.push_back(create_dab(mirror_pt, pressure, tilt_x, -tilt_y, -angle));
  } else if (settings_.symmetry_vertical && settings_.symmetry_horizontal) {
    const Point mirror_v{2.0 * settings_.symmetry_axis_x - point.x, point.y};
    const Point mirror_h{point.x, 2.0 * settings_.symmetry_axis_y - point.y};
    const Point mirror_vh{2.0 * settings_.symmetry_axis_x - point.x,
                          2.0 * settings_.symmetry_axis_y - point.y};
    out_dabs.push_back(create_dab(mirror_v, pressure, -tilt_x, tilt_y, -angle));
    out_dabs.push_back(create_dab(mirror_h, pressure, tilt_x, -tilt_y, -angle));
    out_dabs.push_back(create_dab(mirror_vh, pressure, -tilt_x, -tilt_y, angle));
  }
}

void RoundBrush::stamp(core::TiledImage &image, Point point, double pressure, double tilt_x,
                       double tilt_y, double angle) {
  stamp_single(image, point, pressure, tilt_x, tilt_y, angle);
  if (settings_.symmetry_vertical && !settings_.symmetry_horizontal) {
    const Point mirror_pt{2.0 * settings_.symmetry_axis_x - point.x, point.y};
    stamp_single(image, mirror_pt, pressure, -tilt_x, tilt_y, -angle);
  } else if (!settings_.symmetry_vertical && settings_.symmetry_horizontal) {
    const Point mirror_pt{point.x, 2.0 * settings_.symmetry_axis_y - point.y};
    stamp_single(image, mirror_pt, pressure, tilt_x, -tilt_y, -angle);
  } else if (settings_.symmetry_vertical && settings_.symmetry_horizontal) {
    const Point mirror_v{2.0 * settings_.symmetry_axis_x - point.x, point.y};
    const Point mirror_h{point.x, 2.0 * settings_.symmetry_axis_y - point.y};
    const Point mirror_vh{2.0 * settings_.symmetry_axis_x - point.x,
                          2.0 * settings_.symmetry_axis_y - point.y};
    stamp_single(image, mirror_v, pressure, -tilt_x, tilt_y, -angle);
    stamp_single(image, mirror_h, pressure, tilt_x, -tilt_y, -angle);
    stamp_single(image, mirror_vh, pressure, -tilt_x, -tilt_y, angle);
  }
}

void RoundBrush::stamp_single(core::TiledImage &image, Point point, double pressure, double tilt_x,
                              double tilt_y, double angle) {
  const bool is_stroke_buf = (stroke_buffer_ && &image == stroke_buffer_.get());
  const StrokeInputContext ctx = make_context(pressure, tilt_x, tilt_y);
  const double pressure_curve = std::clamp(settings_.pressure_curve, 0.1, 8.0);
  const double shaped_size = settings_.size_dynamics.active()
                                 ? settings_.size_dynamics.evaluate_context(ctx)
                                 : (settings_.size_curve.active()
                                        ? settings_.size_curve.evaluate(ctx.pressure)
                                        : std::pow(ctx.pressure, pressure_curve));
  const double radius = std::max(0.35, settings_.size * 0.5 * (0.15 + 0.85 * shaped_size));
  const double base_major = radius;
  const double ratio = std::clamp(settings_.ratio, 0.05, 20.0);
  const double base_minor = radius * ratio;

  const double clamped_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const double clamped_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  const double tilt_magnitude = std::min(1.0, std::hypot(clamped_tilt_x, clamped_tilt_y));
  const double tilt_amount =
      settings_.tilt_enabled ? std::clamp(settings_.tilt_strength, 0.0, 1.0) * tilt_magnitude : 0.0;
  const double major_radius = base_major * (1.0 + 0.8 * tilt_amount);
  const double minor_radius = base_minor * std::max(0.2, 1.0 - 0.55 * tilt_amount);
  const double bound = std::max(major_radius, minor_radius);

  const double flow_response = settings_.flow_dynamics.active()
                                   ? settings_.flow_dynamics.evaluate_context(ctx)
                                   : (settings_.opacity_curve.active()
                                          ? settings_.opacity_curve.evaluate(ctx.pressure)
                                          : (0.35 + 0.65 * shaped_size));
  const double flow = std::clamp(settings_.flow, 0.0, 1.0) * flow_response;
  const int min_x = std::max(0, static_cast<int>(std::floor(point.x - bound - 1.0)));
  const int max_x = std::min(image.width() - 1, static_cast<int>(std::ceil(point.x + bound + 1.0)));
  const int min_y = std::max(0, static_cast<int>(std::floor(point.y - bound - 1.0)));
  const int max_y =
      std::min(image.height() - 1, static_cast<int>(std::ceil(point.y + bound + 1.0)));
  if (min_x > max_x || min_y > max_y) {
    return;
  }

  const double tilt_angle = (tilt_magnitude > 0.001) ? std::atan2(clamped_tilt_y, clamped_tilt_x) : 0.0;
  const double final_angle = angle + (tilt_amount > 0.001 ? tilt_angle : 0.0);
  const double cos_tilt = std::cos(final_angle);
  const double sin_tilt = std::sin(final_angle);
  const double bound_sq = bound * bound;
  const double inv_major = 1.0 / major_radius;
  const double inv_minor = 1.0 / minor_radius;
  const double inv_major_sq = inv_major * inv_major;
  const double inv_minor_sq = inv_minor * inv_minor;
  const bool is_smudge_mode = (settings_.mode == Mode::SMUDGE);
  const bool is_wet_paint = (settings_.mode == Mode::PAINT && settings_.color_rate < 0.999);
  const bool needs_pickup = is_smudge_mode || is_wet_paint;

  core::Rgba8 dab_color = settings_.color;
  double shift_x = point.x - last_dab_point_.x;
  double shift_y = point.y - last_dab_point_.y;
  const double shift_len = std::hypot(shift_x, shift_y);
  if (shift_len < 0.5) {
    shift_x = std::cos(final_angle) * (radius * 0.25);
    shift_y = std::sin(final_angle) * (radius * 0.25);
  } else {
    const double max_shift = radius * 0.75;
    if (shift_len > max_shift) {
      shift_x = (shift_x / shift_len) * max_shift;
      shift_y = (shift_y / shift_len) * max_shift;
    }
  }

  double eff_smudge_rate = settings_.smudge_rate;
  if (settings_.smudge_dynamics.active()) {
    eff_smudge_rate *= settings_.smudge_dynamics.evaluate_context(ctx);
  } else {
    eff_smudge_rate *= (0.3 + 0.7 * ctx.pressure);
  }
  eff_smudge_rate = std::clamp(eff_smudge_rate, 0.02, 0.98);

  if (needs_pickup) {
    double pickup_r = 0.0, pickup_g = 0.0, pickup_b = 0.0, pickup_a = 0.0;
    double pickup_weight = 0.0;
    const int sample_step = (bound > 32.0) ? 2 : 1;

    for (int y = min_y; y <= max_y; y += sample_step) {
      const double dy = (static_cast<double>(y) + 0.5) - point.y;
      const double dy_sq = dy * dy;
      for (int x = min_x; x <= max_x; x += sample_step) {
        const double dx = (static_cast<double>(x) + 0.5) - point.x;
        if (dx * dx + dy_sq > bound_sq) continue;
        const double along = dx * cos_tilt + dy * sin_tilt;
        const double across = -dx * sin_tilt + dy * cos_tilt;
        const double norm_x = along * inv_major;
        const double norm_y = across * inv_minor;
        const double norm_sq = norm_x * norm_x + norm_y * norm_y;
        if (norm_sq <= 1.0) {
          const double normalized = std::sqrt(norm_sq);
          const core::TiledImage &pickup_source = (image_ && is_stroke_buf) ? *image_ : image;
          const auto px = pickup_source.pixel(x, y);
          if (px.a > 0) {
            const double w = (1.0 - normalized) * (px.a / 255.0);
            pickup_r += px.r * w;
            pickup_g += px.g * w;
            pickup_b += px.b * w;
            pickup_a += px.a * w;
            pickup_weight += w;
          }
        }
      }
    }

    if (pickup_weight > 0.0) {
      const core::Rgba8 canvas_color{
          static_cast<std::uint8_t>(std::clamp(std::lround(pickup_r / pickup_weight), 0L, 255L)),
          static_cast<std::uint8_t>(std::clamp(std::lround(pickup_g / pickup_weight), 0L, 255L)),
          static_cast<std::uint8_t>(std::clamp(std::lround(pickup_b / pickup_weight), 0L, 255L)),
          static_cast<std::uint8_t>(std::clamp(std::lround(pickup_a / pickup_weight), 0L, 255L))
      };

      if (is_smudge_mode) {
        if (has_reservoir_) {
          const double sr = eff_smudge_rate * reservoir_wetness_;
          dab_color.r = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - sr) * canvas_color.r + sr * reservoir_color_.r), 0L, 255L));
          dab_color.g = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - sr) * canvas_color.g + sr * reservoir_color_.g), 0L, 255L));
          dab_color.b = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - sr) * canvas_color.b + sr * reservoir_color_.b), 0L, 255L));
          dab_color.a = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - sr) * canvas_color.a + sr * reservoir_color_.a), 0L, 255L));
        } else {
          dab_color = canvas_color;
        }
        reservoir_color_ = dab_color;
        has_reservoir_ = true;
      } else if (is_wet_paint) {
        const double cr = std::clamp(settings_.color_rate, 0.0, 1.0);
        const core::Rgba8 source_color = has_reservoir_
            ? core::Rgba8{
                  static_cast<std::uint8_t>(std::clamp(std::lround(0.5 * canvas_color.r + 0.5 * reservoir_color_.r), 0L, 255L)),
                  static_cast<std::uint8_t>(std::clamp(std::lround(0.5 * canvas_color.g + 0.5 * reservoir_color_.g), 0L, 255L)),
                  static_cast<std::uint8_t>(std::clamp(std::lround(0.5 * canvas_color.b + 0.5 * reservoir_color_.b), 0L, 255L)),
                  static_cast<std::uint8_t>(std::clamp(std::lround(0.5 * canvas_color.a + 0.5 * reservoir_color_.a), 0L, 255L))
              }
            : canvas_color;

        dab_color.r = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - cr) * source_color.r + cr * settings_.color.r), 0L, 255L));
        dab_color.g = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - cr) * source_color.g + cr * settings_.color.g), 0L, 255L));
        dab_color.b = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - cr) * source_color.b + cr * settings_.color.b), 0L, 255L));
        dab_color.a = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - cr) * source_color.a + cr * settings_.color.a), 0L, 255L));

        reservoir_color_ = dab_color;
        has_reservoir_ = true;
      }
    } else {
      if (is_smudge_mode) {
        if (has_reservoir_) {
          dab_color = reservoir_color_;
          dab_color.a = static_cast<std::uint8_t>(std::clamp(std::lround(reservoir_color_.a * reservoir_wetness_), 0L, 255L));
          if (dab_color.a == 0) {
            return;
          }
        } else {
          return;
        }
      }
    }
  }

  int last_tx = -1;
  int last_ty = -1;
  core::Tile *cached_tile = nullptr;
  const bool alpha_locked = image.alpha_locked();
  for (int y = min_y; y <= max_y; ++y) {
    const double dy = (static_cast<double>(y) + 0.5) - point.y;
    const double dy_sq = dy * dy;
    const int ty = y / core::kTileSize;
    const int ly = y % core::kTileSize;

    for (int x = min_x; x <= max_x; ++x) {
      const double dx = (static_cast<double>(x) + 0.5) - point.x;
      if (dx * dx + dy_sq > bound_sq) {
        continue;
      }
      const double along = dx * cos_tilt + dy * sin_tilt;
      const double across = -dx * sin_tilt + dy * cos_tilt;
      const double norm_x = along * inv_major;
      const double norm_y = across * inv_minor;
      const double norm_sq = norm_x * norm_x + norm_y * norm_y;
      if (norm_sq > 1.0) {
        continue;
      }
      const double normalized = std::sqrt(norm_sq);

      double dir_hardness = settings_.hardness;
      if (norm_sq > 1e-9) {
        dir_hardness = (norm_x * norm_x * settings_.hardness_x + norm_y * norm_y * settings_.hardness_y) / norm_sq;
      }
      dir_hardness = std::clamp(dir_hardness, 0.0, 1.0);

      double coverage = 1.0;
      if (normalized > dir_hardness) {
        coverage = 1.0 - (normalized - dir_hardness) / std::max(0.0001, 1.0 - dir_hardness);
      }
      if (settings_.tip && !settings_.tip->empty()) {
        const double u = (along * inv_major) * 0.5 + 0.5;
        const double v = (across * inv_minor) * 0.5 + 0.5;
        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
          coverage *= settings_.tip->sample(u, v);
        } else {
          coverage = 0.0;
        }
      }
      if (settings_.texture_enabled && settings_.texture && !settings_.texture->empty()) {
        const double raw_t = settings_.texture->sample(
            static_cast<double>(x), static_cast<double>(y),
            settings_.texture_scale, settings_.texture_offset_x, settings_.texture_offset_y);

        double t = (raw_t - 0.5) * settings_.texture_contrast + 0.5 + settings_.texture_brightness;
        t = std::clamp(t, 0.0, 1.0);
        if (settings_.texture_invert) {
          t = 1.0 - t;
        }

        const double pressure_influence = 0.4 + 0.6 * (1.0 - ctx.pressure);
        const double effective_strength = std::clamp(settings_.texture_strength * pressure_influence, 0.0, 1.0);
        coverage *= ((1.0 - effective_strength) + effective_strength * t);
      }

      if (selection_ && selection_->active()) {
        const std::uint8_t sel = selection_->value(x, y);
        if (sel == 0) {
          continue;
        }
        if (sel < 255) {
          coverage *= (sel / 255.0);
        }
      }
      if (coverage <= 0.0) {
        continue;
      }

      const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(
          std::lround(static_cast<double>(dab_color.a) * flow * coverage), 0L, 255L));
      if (alpha == 0) {
        continue;
      }

      const int tx = x / core::kTileSize;
      const int lx = x % core::kTileSize;

      if (tx != last_tx || ty != last_ty) {
        last_tx = tx;
        last_ty = ty;
        cached_tile = image.writable_tile_at(tx, ty);
      }

      if (cached_tile != nullptr) {
        core::Rgba8 &dest = cached_tile->pixels[static_cast<std::size_t>(ly) * core::kTileSize + lx];
        if (is_stroke_buf) {
          const double dab_rate = std::clamp(flow * coverage, 0.0, 1.0);
          const double cur_a = dest.a / 255.0;
          const double delta_a = dab_rate * (1.0 - cur_a);
          const double new_a = cur_a + delta_a;

          if (settings_.mode == Mode::ERASE) {
            dest.r = 0;
            dest.g = 0;
            dest.b = 0;
            dest.a = static_cast<std::uint8_t>(std::clamp(std::lround(new_a * 255.0), 0L, 255L));
          } else {
            const double cur_r = (dest.r / 255.0) * cur_a;
            const double cur_g = (dest.g / 255.0) * cur_a;
            const double cur_b = (dest.b / 255.0) * cur_a;
            const double src_r = dab_color.r / 255.0;
            const double src_g = dab_color.g / 255.0;
            const double src_b = dab_color.b / 255.0;
            const double new_r = cur_r + src_r * delta_a;
            const double new_g = cur_g + src_g * delta_a;
            const double new_b = cur_b + src_b * delta_a;
            if (new_a > 1e-5) {
              dest.r = static_cast<std::uint8_t>(std::clamp(std::lround((new_r / new_a) * 255.0), 0L, 255L));
              dest.g = static_cast<std::uint8_t>(std::clamp(std::lround((new_g / new_a) * 255.0), 0L, 255L));
              dest.b = static_cast<std::uint8_t>(std::clamp(std::lround((new_b / new_a) * 255.0), 0L, 255L));
              dest.a = static_cast<std::uint8_t>(std::clamp(std::lround(new_a * 255.0), 0L, 255L));
            }
          }
          continue;
        }

        if (settings_.mode == Mode::ERASE) {
          if (!alpha_locked) {
            dest.a = static_cast<std::uint8_t>(std::max(0, static_cast<int>(dest.a) - alpha));
            if (dest.a == 0) {
              dest.r = 0;
              dest.g = 0;
              dest.b = 0;
            }
          }
        } else {
          const std::uint8_t orig_a = dest.a;
          if (alpha_locked && orig_a == 0) {
            continue;
          }

          core::Rgba8 write_color = dab_color;
          if (settings_.mode == Mode::SMUDGE && settings_.smudge_mode == SmudgeMode::SMEAR) {
            const int isx = static_cast<int>(std::lround(static_cast<double>(x) - shift_x));
            const int isy = static_cast<int>(std::lround(static_cast<double>(y) - shift_y));
            if (isx >= 0 && isx < image.width() && isy >= 0 && isy < image.height()) {
              const auto spx = image.pixel(isx, isy);
              if (spx.a > 0) {
                write_color = spx;
                if (has_reservoir_) {
                  write_color.r = static_cast<std::uint8_t>((write_color.r * 3 + reservoir_color_.r) / 4);
                  write_color.g = static_cast<std::uint8_t>((write_color.g * 3 + reservoir_color_.g) / 4);
                  write_color.b = static_cast<std::uint8_t>((write_color.b * 3 + reservoir_color_.b) / 4);
                }
                write_color.a = static_cast<std::uint8_t>(std::clamp(std::lround(write_color.a * reservoir_wetness_), 0L, 255L));
              }
            }
          }

          const std::uint8_t eff_alpha = (settings_.mode == Mode::SMUDGE)
              ? static_cast<std::uint8_t>(std::clamp(
                    std::lround(static_cast<double>(write_color.a) * flow * coverage * eff_smudge_rate), 0L, 255L))
              : alpha;

          if (eff_alpha > 0) {
            if (dest.a == 0) {
              dest.r = write_color.r;
              dest.g = write_color.g;
              dest.b = write_color.b;
              dest.a = eff_alpha;
            } else if (eff_alpha == 255 && dest.a == 255) {
              dest.r = write_color.r;
              dest.g = write_color.g;
              dest.b = write_color.b;
              dest.a = 255;
            } else {
              const float sa = static_cast<float>(eff_alpha) / 255.0F;
              const float da = static_cast<float>(dest.a) / 255.0F;
              const float out_a = sa + da * (1.0F - sa);
              if (out_a > 0.0F) {
                const float inv_out = 1.0F / out_a;
                dest.r = static_cast<std::uint8_t>(std::clamp(std::lround((write_color.r * sa + dest.r * da * (1.0F - sa)) * inv_out), 0L, 255L));
                dest.g = static_cast<std::uint8_t>(std::clamp(std::lround((write_color.g * sa + dest.g * da * (1.0F - sa)) * inv_out), 0L, 255L));
                dest.b = static_cast<std::uint8_t>(std::clamp(std::lround((write_color.b * sa + dest.b * da * (1.0F - sa)) * inv_out), 0L, 255L));
                dest.a = static_cast<std::uint8_t>(std::clamp(std::lround(out_a * 255.0F), 0L, 255L));
              }
            }

            if (alpha_locked) {
              dest.a = orig_a;
            }
          }
        }
      }
    }
  }
  if (is_smudge_mode) {
    reservoir_wetness_ = std::max(0.05, reservoir_wetness_ * 0.96);
  }
}

} // namespace kreart::brush
