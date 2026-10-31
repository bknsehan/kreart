#include "kreart/core/document.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string_view>
#include <utility>

namespace kreart::core {
namespace {

// cos_a/sin_a are precomputed once per layer by the caller.
inline Rgba8 apply_screentone(Rgba8 src, int gx, int gy, double freq, double cos_a,
                              double sin_a, ScreentonePattern pat) {
  if (src.a == 0) return Rgba8{0, 0, 0, 0};
  const double u = gx * cos_a - gy * sin_a;
  const double v = gx * sin_a + gy * cos_a;

  double eff_freq = freq;
  if (pat == ScreentonePattern::DOT_50) eff_freq = 50.0;
  else if (pat == ScreentonePattern::DOT_40) eff_freq = 40.0;
  else if (pat == ScreentonePattern::DOT_80) eff_freq = 80.0;
  else if (pat == ScreentonePattern::DOT_60) eff_freq = 60.0;

  const double period = std::max(2.0, 100.0 / std::max(5.0, eff_freq));
  const double gray = 1.0 - (0.299 * src.r + 0.587 * src.g + 0.114 * src.b) / 255.0;
  const double weight = gray * (src.a / 255.0);

  if (pat == ScreentonePattern::LINE_45) {
    const double cu = std::fmod(std::fmod(u, period) + period, period);
    if (cu <= period * std::clamp(weight, 0.0, 1.0)) {
      return Rgba8{src.r, src.g, src.b, 255};
    }
    return Rgba8{0, 0, 0, 0};
  }

  if (pat == ScreentonePattern::CROSSHATCH) {
    const double cu = std::fmod(std::fmod(u, period) + period, period);
    const double cv = std::fmod(std::fmod(v, period) + period, period);
    const double w_half = period * std::clamp(weight * 0.5, 0.0, 0.5);
    if (cu <= w_half || cv <= w_half) {
      return Rgba8{src.r, src.g, src.b, 255};
    }
    return Rgba8{0, 0, 0, 0};
  }

  double cu = std::fmod(std::fmod(u, period) + period, period) - period * 0.5;
  double cv = std::fmod(std::fmod(v, period) + period, period) - period * 0.5;
  const double dist = std::hypot(cu, cv);
  const double max_r = period * 0.70710678;
  const double dot_r = max_r * std::sqrt(std::clamp(weight, 0.0, 1.0));
  if (dist <= dot_r) {
    return Rgba8{src.r, src.g, src.b, 255};
  }
  return Rgba8{0, 0, 0, 0};
}
enum class BlendOp {
  NORMAL,
  MULTIPLY,
  SCREEN,
  ADD,
  OVERLAY,
  SOFT_LIGHT,
  DARKEN,
  LIGHTEN,
  COLOR_BURN,
  COLOR_DODGE
};

// Resolved once per layer instead of comparing strings per pixel.
inline BlendOp blend_op_from_string(std::string_view op) {
  if (op == "multiply") return BlendOp::MULTIPLY;
  if (op == "screen") return BlendOp::SCREEN;
  if (op == "add" || op == "linear_dodge") return BlendOp::ADD;
  if (op == "overlay") return BlendOp::OVERLAY;
  if (op == "soft_light") return BlendOp::SOFT_LIGHT;
  if (op == "darken") return BlendOp::DARKEN;
  if (op == "lighten") return BlendOp::LIGHTEN;
  if (op == "color_burn") return BlendOp::COLOR_BURN;
  if (op == "color_dodge") return BlendOp::COLOR_DODGE;
  return BlendOp::NORMAL;
}

inline void blend_layer_pixel(Rgba8 &dst, Rgba8 src, double layer_opacity,
                             BlendOp blend_mode, double clip_alpha) {
  const double effective_sa = (src.a / 255.0) * layer_opacity * clip_alpha;
  if (effective_sa <= 0.001) {
    return;
  }

  if (dst.a == 0) {
    dst = Rgba8{src.r, src.g, src.b, static_cast<std::uint8_t>(std::clamp(effective_sa * 255.0 + 0.5, 0.0, 255.0))};
    return;
  }

  const double da = dst.a / 255.0;
  const double out_a = effective_sa + da * (1.0 - effective_sa);
  if (out_a <= 0.001) {
    dst = Rgba8{0, 0, 0, 0};
    return;
  }

  const double sr = src.r / 255.0;
  const double sg = src.g / 255.0;
  const double sb = src.b / 255.0;
  const double dr = dst.r / 255.0;
  const double dg = dst.g / 255.0;
  const double db = dst.b / 255.0;

  double br = sr, bg = sg, bb = sb;

  switch (blend_mode) {
  case BlendOp::MULTIPLY:
    br = sr * dr;
    bg = sg * dg;
    bb = sb * db;
    break;
  case BlendOp::SCREEN:
    br = 1.0 - (1.0 - sr) * (1.0 - dr);
    bg = 1.0 - (1.0 - sg) * (1.0 - dg);
    bb = 1.0 - (1.0 - sb) * (1.0 - db);
    break;
  case BlendOp::ADD:
    br = std::min(1.0, sr + dr);
    bg = std::min(1.0, sg + dg);
    bb = std::min(1.0, sb + db);
    break;
  case BlendOp::OVERLAY: {
    auto overlay_ch = [](double s, double d) {
      return (d < 0.5) ? (2.0 * s * d) : (1.0 - 2.0 * (1.0 - s) * (1.0 - d));
    };
    br = overlay_ch(sr, dr);
    bg = overlay_ch(sg, dg);
    bb = overlay_ch(sb, db);
    break;
  }
  case BlendOp::SOFT_LIGHT: {
    auto soft_ch = [](double s, double d) {
      return (1.0 - 2.0 * s) * d * d + 2.0 * s * d;
    };
    br = soft_ch(sr, dr);
    bg = soft_ch(sg, dg);
    bb = soft_ch(sb, db);
    break;
  }
  case BlendOp::DARKEN:
    br = std::min(sr, dr);
    bg = std::min(sg, dg);
    bb = std::min(sb, db);
    break;
  case BlendOp::LIGHTEN:
    br = std::max(sr, dr);
    bg = std::max(sg, dg);
    bb = std::max(sb, db);
    break;
  case BlendOp::COLOR_BURN: {
    auto burn_ch = [](double s, double d) {
      return (s <= 0.0) ? 0.0 : std::max(0.0, 1.0 - (1.0 - d) / s);
    };
    br = burn_ch(sr, dr);
    bg = burn_ch(sg, dg);
    bb = burn_ch(sb, db);
    break;
  }
  case BlendOp::COLOR_DODGE: {
    auto dodge_ch = [](double s, double d) {
      return (s >= 1.0) ? 1.0 : std::min(1.0, d / (1.0 - s));
    };
    br = dodge_ch(sr, dr);
    bg = dodge_ch(sg, dg);
    bb = dodge_ch(sb, db);
    break;
  }
  case BlendOp::NORMAL:
  default:
    break;
  }

  const double out_r = (br * effective_sa + dr * da * (1.0 - effective_sa)) / out_a;
  const double out_g = (bg * effective_sa + dg * da * (1.0 - effective_sa)) / out_a;
  const double out_b = (bb * effective_sa + db * da * (1.0 - effective_sa)) / out_a;

  dst.r = static_cast<std::uint8_t>(std::clamp(out_r * 255.0 + 0.5, 0.0, 255.0));
  dst.g = static_cast<std::uint8_t>(std::clamp(out_g * 255.0 + 0.5, 0.0, 255.0));
  dst.b = static_cast<std::uint8_t>(std::clamp(out_b * 255.0 + 0.5, 0.0, 255.0));
  dst.a = static_cast<std::uint8_t>(std::clamp(out_a * 255.0 + 0.5, 0.0, 255.0));
}

bool snapshots_differ(const DocumentSnapshot &a, const DocumentSnapshot &b) {
  if (a.width != b.width || a.height != b.height || a.active_layer_index != b.active_layer_index) {
    return true;
  }
  if (a.selection != b.selection || a.floating_selection != b.floating_selection) {
    return true;
  }
  if (a.layers.size() != b.layers.size()) {
    return true;
  }
  for (std::size_t i = 0; i < a.layers.size(); ++i) {
    const auto &la = a.layers[i];
    const auto &lb = b.layers[i];
    if (la.name != lb.name || la.visible != lb.visible || la.opacity != lb.opacity ||
        la.composite_op != lb.composite_op || la.locked != lb.locked ||
        la.alpha_locked != lb.alpha_locked || la.clipping_mask != lb.clipping_mask ||
        la.screentone != lb.screentone || la.screentone_frequency != lb.screentone_frequency ||
        la.screentone_angle != lb.screentone_angle || la.screentone_pattern != lb.screentone_pattern ||
        la.is_group != lb.is_group || la.expanded != lb.expanded || la.parent_id != lb.parent_id) {
      return true;
    }
    if (la.image.tiles != lb.image.tiles) {
      return true;
    }
  }
  return false;
}

} // namespace

Document::Document(int width, int height)
    : width_(std::max(1, width)), height_(std::max(1, height)), composite_(width_, height_) {
  reset(width_, height_);
}

void Document::reset(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  layers_.clear();
  next_layer_id_ = 1;
  layers_.emplace_back("1", width_, height_, false);
  layers_.back().set_id(1);
  has_active_layer_ = true;
  composite_ = TiledImage(width_, height_);
  touch_composite();
  selection_.reset(width_, height_);
  pending_.reset();
  undo_.clear();
  redo_.clear();
  modified_ = false;
}
void Document::resize_canvas(int new_width, int new_height, int offset_x, int offset_y) {
  new_width = std::max(1, new_width);
  new_height = std::max(1, new_height);
  if (width_ == new_width && height_ == new_height && offset_x == 0 && offset_y == 0) {
    return;
  }

  begin_change();
  width_ = new_width;
  height_ = new_height;
  for (auto &layer : layers_) {
    layer.image().resize(width_, height_, offset_x, offset_y);
  }
  composite_ = TiledImage(width_, height_);
  touch_composite();
  selection_.reset(width_, height_);
  commit_change();
}
void Document::scale_image(int new_width, int new_height, ScaleFilter filter) {
  new_width = std::max(1, new_width);
  new_height = std::max(1, new_height);
  if (width_ == new_width && height_ == new_height) {
    return;
  }

  begin_change();

  width_ = new_width;
  height_ = new_height;
  for (auto &layer : layers_) {
    layer.image().scale(new_width, new_height, filter);
  }

  selection_.scale(new_width, new_height);
  composite_ = TiledImage(width_, height_);
  touch_composite();
  modified_ = true;
  commit_change();
}
void Document::clear_selection_content() {
  if (!selection_.has_selection()) {
    return;
  }
  begin_change();
  auto &img = active_layer().image();
  const auto &b = selection_.bounds();
  for (int y = b.y; y < b.y + b.height && y < height_; ++y) {
    if (y < 0) continue;
    for (int x = b.x; x < b.x + b.width && x < width_; ++x) {
      if (x < 0) continue;
      if (selection_.is_selected(x, y)) {
        img.erase_pixel(x, y, selection_.value(x, y));
      }
    }
  }
  touch_composite();
  commit_change();
}
void Document::start_move_selection() {
  if (floating_selection_.active || !selection_.has_selection()) {
    return;
  }
  const auto &b = selection_.bounds();
  if (b.width <= 0 || b.height <= 0) {
    return;
  }

  begin_change();
  floating_selection_.active = true;
  floating_selection_.origin_x = b.x;
  floating_selection_.origin_y = b.y;
  floating_selection_.offset_x = 0;
  floating_selection_.offset_y = 0;
  floating_selection_.width = b.width;
  floating_selection_.height = b.height;
  floating_selection_.rotation = 0.0;
  floating_selection_.scale_x = 1.0;
  floating_selection_.scale_y = 1.0;
  floating_selection_.pivot_x = b.x + b.width / 2.0;
  floating_selection_.pivot_y = b.y + b.height / 2.0;
  const std::size_t count = static_cast<std::size_t>(b.width) * b.height;
  floating_selection_.pixels.assign(count, Rgba8{0, 0, 0, 0});
  floating_selection_.mask.assign(count, 0);

  const int clamp_x0 = std::max(0, b.x);
  const int clamp_y0 = std::max(0, b.y);
  const int clamp_x1 = std::min(width_, b.x + b.width);
  const int clamp_y1 = std::min(height_, b.y + b.height);

  if (clamp_x0 < clamp_x1 && clamp_y0 < clamp_y1) {
    const auto &mask_data = selection_.data();
    for (int y = clamp_y0; y < clamp_y1; ++y) {
      const int fs_y = y - b.y;
      const std::size_t dst_row = static_cast<std::size_t>(fs_y) * b.width + (clamp_x0 - b.x);
      const std::size_t row_len = static_cast<std::size_t>(clamp_x1 - clamp_x0);
      if (!mask_data.empty()) {
        const std::size_t src_row = static_cast<std::size_t>(y) * width_ + clamp_x0;
        std::memcpy(&floating_selection_.mask[dst_row], &mask_data[src_row], row_len);
      } else {
        std::memset(&floating_selection_.mask[dst_row], 255, row_len);
      }
    }

    auto &img = active_layer().image();
    const int start_tx = clamp_x0 / kTileSize;
    const int end_tx = (clamp_x1 - 1) / kTileSize;
    const int start_ty = clamp_y0 / kTileSize;
    const int end_ty = (clamp_y1 - 1) / kTileSize;

    for (int ty = start_ty; ty <= end_ty; ++ty) {
      for (int tx = start_tx; tx <= end_tx; ++tx) {
        if (img.tile_at(tx, ty) == nullptr) {
          continue;
        }
        Tile *tile = img.writable_tile_at(tx, ty);
        if (!tile) {
          continue;
        }

        const int tile_x0 = tx * kTileSize;
        const int tile_y0 = ty * kTileSize;
        const int inter_x0 = std::max(clamp_x0, tile_x0);
        const int inter_x1 = std::min(clamp_x1, tile_x0 + kTileSize);
        const int inter_y0 = std::max(clamp_y0, tile_y0);
        const int inter_y1 = std::min(clamp_y1, tile_y0 + kTileSize);

        bool tile_modified = false;
        for (int y = inter_y0; y < inter_y1; ++y) {
          const int local_y = y - tile_y0;
          const int fs_y = y - b.y;
          const std::size_t tile_row = static_cast<std::size_t>(local_y) * kTileSize;
          const std::size_t fs_row = static_cast<std::size_t>(fs_y) * b.width;

          for (int x = inter_x0; x < inter_x1; ++x) {
            const int fs_x = x - b.x;
            const std::size_t fs_idx = fs_row + fs_x;
            if (floating_selection_.mask[fs_idx] == 0) {
              continue;
            }
            const int local_x = x - tile_x0;
            const std::size_t tile_idx = tile_row + local_x;
            floating_selection_.pixels[fs_idx] = tile->pixels[tile_idx];
            tile->pixels[tile_idx] = Rgba8{0, 0, 0, 0};
            tile_modified = true;
          }
        }
        if (tile_modified) {
          img.mark_tile_dirty(tx, ty);
        }
      }
    }
  }
  touch_composite();
}

void Document::update_move_selection(int delta_x, int delta_y) {
  if (!floating_selection_.active) {
    return;
  }
  floating_selection_.offset_x = delta_x;
  floating_selection_.offset_y = delta_y;
}

template <typename BlendTarget>
static void render_transformed_floating_selection(const FloatingSelection &fs, BlendTarget &target,
                                                  int doc_w, int doc_h) {
  const double cos_r = std::cos(fs.rotation);
  const double sin_r = std::sin(fs.rotation);
  const double cu = fs.width / 2.0;
  const double cv = fs.height / 2.0;
  const double sx = (std::abs(fs.scale_x) < 1e-5) ? 1e-5 : fs.scale_x;
  const double sy = (std::abs(fs.scale_y) < 1e-5) ? 1e-5 : fs.scale_y;

  const double corners_u[4] = {0.0, static_cast<double>(fs.width), static_cast<double>(fs.width), 0.0};
  const double corners_v[4] = {0.0, 0.0, static_cast<double>(fs.height), static_cast<double>(fs.height)};

  double min_x = 1e9, max_x = -1e9;
  double min_y = 1e9, max_y = -1e9;

  for (int i = 0; i < 4; ++i) {
    const double rel_x = (corners_u[i] - cu) * sx;
    const double rel_y = (corners_v[i] - cv) * sy;
    const double X = fs.pivot_x + fs.offset_x + cos_r * rel_x - sin_r * rel_y;
    const double Y = fs.pivot_y + fs.offset_y + sin_r * rel_x + cos_r * rel_y;
    min_x = std::min(min_x, X);
    max_x = std::max(max_x, X);
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }

  const int start_x = std::clamp(static_cast<int>(std::floor(min_x)), 0, doc_w - 1);
  const int end_x = std::clamp(static_cast<int>(std::ceil(max_x)), 0, doc_w - 1);
  const int start_y = std::clamp(static_cast<int>(std::floor(min_y)), 0, doc_h - 1);
  const int end_y = std::clamp(static_cast<int>(std::ceil(max_y)), 0, doc_h - 1);

  for (int y = start_y; y <= end_y; ++y) {
    for (int x = start_x; x <= end_x; ++x) {
      const double dx = x - (fs.pivot_x + fs.offset_x);
      const double dy = y - (fs.pivot_y + fs.offset_y);
      const double rot_x = cos_r * dx + sin_r * dy;
      const double rot_y = -sin_r * dx + cos_r * dy;
      const double u = cu + rot_x / sx;
      const double v = cv + rot_y / sy;

      if (u < -0.5 || u >= fs.width - 0.5 || v < -0.5 || v >= fs.height - 0.5) {
        continue;
      }

      const int x0 = static_cast<int>(std::floor(u));
      const int y0 = static_cast<int>(std::floor(v));
      const int x1 = x0 + 1;
      const int y1 = y0 + 1;
      const double fx = u - x0;
      const double fy = v - y0;

      const double w00 = (1.0 - fx) * (1.0 - fy);
      const double w10 = fx * (1.0 - fy);
      const double w01 = (1.0 - fx) * fy;
      const double w11 = fx * fy;

      double acc_r = 0.0, acc_g = 0.0, acc_b = 0.0, acc_a = 0.0;

      auto sample_pt = [&](int cx, int cy, double wt) {
        if (cx >= 0 && cx < fs.width && cy >= 0 && cy < fs.height) {
          const std::size_t idx = static_cast<std::size_t>(cy) * fs.width + cx;
          const auto &p = fs.pixels[idx];
          const std::uint8_t m = fs.mask[idx];
          if (m > 0 && p.a > 0) {
            const double eff_a = (p.a * m) / 255.0;
            acc_a += wt * eff_a;
            acc_r += wt * p.r * eff_a;
            acc_g += wt * p.g * eff_a;
            acc_b += wt * p.b * eff_a;
          }
        }
      };

      sample_pt(x0, y0, w00);
      sample_pt(x1, y0, w10);
      sample_pt(x0, y1, w01);
      sample_pt(x1, y1, w11);

      if (acc_a >= 1.0) {
        const std::uint8_t fa = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(acc_a)), 0, 255));
        const std::uint8_t fr = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(acc_r / acc_a)), 0, 255));
        const std::uint8_t fg = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(acc_g / acc_a)), 0, 255));
        const std::uint8_t fb = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(acc_b / acc_a)), 0, 255));
        target.blend_pixel(x, y, Rgba8{fr, fg, fb, fa});
      }
    }
  }
}

void Document::update_transform_selection(int delta_x, int delta_y, double rotation,
                                          double scale_x, double scale_y) {
  if (!floating_selection_.active) {
    return;
  }
  floating_selection_.offset_x = delta_x;
  floating_selection_.offset_y = delta_y;
  floating_selection_.rotation = rotation;
  floating_selection_.scale_x = scale_x;
  floating_selection_.scale_y = scale_y;
}

void Document::rotate_selection(double delta_radians) {
  if (!selection_.active()) return;
  if (!floating_selection_.active) {
    start_move_selection();
  }
  floating_selection_.rotation += delta_radians;
}

void Document::scale_selection(double factor_x, double factor_y) {
  if (!selection_.active()) return;
  if (!floating_selection_.active) {
    start_move_selection();
  }
  floating_selection_.scale_x *= factor_x;
  floating_selection_.scale_y *= factor_y;
}

void Document::commit_move_selection() {
  if (!floating_selection_.active) {
    return;
  }
  auto &img = active_layer().image();
  const bool is_pure_translation =
      (std::abs(floating_selection_.rotation) < 1e-6 &&
       std::abs(floating_selection_.scale_x - 1.0) < 1e-6 &&
       std::abs(floating_selection_.scale_y - 1.0) < 1e-6);

  if (is_pure_translation && floating_selection_.offset_x == 0 && floating_selection_.offset_y == 0) {
    const int target_ox = floating_selection_.origin_x;
    const int target_oy = floating_selection_.origin_y;
    const int fs_w = floating_selection_.width;
    const int fs_h = floating_selection_.height;
    for (int y = 0; y < fs_h; ++y) {
      const int doc_y = target_oy + y;
      if (doc_y < 0 || doc_y >= height_) continue;
      for (int x = 0; x < fs_w; ++x) {
        const int doc_x = target_ox + x;
        if (doc_x < 0 || doc_x >= width_) continue;
        const std::size_t idx = static_cast<std::size_t>(y) * fs_w + x;
        if (floating_selection_.mask[idx] > 0 && floating_selection_.pixels[idx].a > 0) {
          img.set_pixel(doc_x, doc_y, floating_selection_.pixels[idx]);
        }
      }
    }
    floating_selection_.active = false;
    floating_selection_.pixels.clear();
    floating_selection_.mask.clear();
    touch_composite();
    cancel_change();
    return;
  }
  if (is_pure_translation) {
    const int target_ox = floating_selection_.origin_x + floating_selection_.offset_x;
    const int target_oy = floating_selection_.origin_y + floating_selection_.offset_y;
    const int fs_w = floating_selection_.width;
    const int fs_h = floating_selection_.height;

    const int clamp_x0 = std::max(0, target_ox);
    const int clamp_y0 = std::max(0, target_oy);
    const int clamp_x1 = std::min(width_, target_ox + fs_w);
    const int clamp_y1 = std::min(height_, target_oy + fs_h);

    if (clamp_x0 < clamp_x1 && clamp_y0 < clamp_y1) {
      const int start_tx = clamp_x0 / kTileSize;
      const int end_tx = (clamp_x1 - 1) / kTileSize;
      const int start_ty = clamp_y0 / kTileSize;
      const int end_ty = (clamp_y1 - 1) / kTileSize;

      for (int ty = start_ty; ty <= end_ty; ++ty) {
        for (int tx = start_tx; tx <= end_tx; ++tx) {
          Tile *tile = img.writable_tile_at(tx, ty);
          if (!tile) continue;

          const int tile_x0 = tx * kTileSize;
          const int tile_y0 = ty * kTileSize;
          const int inter_x0 = std::max(clamp_x0, tile_x0);
          const int inter_x1 = std::min(clamp_x1, tile_x0 + kTileSize);
          const int inter_y0 = std::max(clamp_y0, tile_y0);
          const int inter_y1 = std::min(clamp_y1, tile_y0 + kTileSize);

          bool tile_modified = false;
          for (int y = inter_y0; y < inter_y1; ++y) {
            const int local_y = y - tile_y0;
            const int fs_y = y - target_oy;
            const std::size_t tile_row = static_cast<std::size_t>(local_y) * kTileSize;
            const std::size_t fs_row = static_cast<std::size_t>(fs_y) * fs_w;

            for (int x = inter_x0; x < inter_x1; ++x) {
              const int fs_x = x - target_ox;
              const std::size_t fs_idx = fs_row + fs_x;
              if (floating_selection_.mask[fs_idx] == 0) {
                continue;
              }
              const Rgba8 &src_px = floating_selection_.pixels[fs_idx];
              if (src_px.a == 0) {
                continue;
              }
              const int local_x = x - tile_x0;
              const std::size_t tile_idx = tile_row + local_x;
              Rgba8 &dst_px = tile->pixels[tile_idx];

              if (img.alpha_locked()) {
                if (dst_px.a == 0) continue;
                const float sa = static_cast<float>(src_px.a) / 255.0F;
                const float inv_sa = 1.0F - sa;
                dst_px.r = static_cast<std::uint8_t>(std::clamp(
                    std::lround(src_px.r * sa + dst_px.r * inv_sa), 0L, 255L));
                dst_px.g = static_cast<std::uint8_t>(std::clamp(
                    std::lround(src_px.g * sa + dst_px.g * inv_sa), 0L, 255L));
                dst_px.b = static_cast<std::uint8_t>(std::clamp(
                    std::lround(src_px.b * sa + dst_px.b * inv_sa), 0L, 255L));
              } else {
                if (src_px.a == 255 || dst_px.a == 0) {
                  dst_px = src_px;
                } else {
                  const float sa = static_cast<float>(src_px.a) / 255.0F;
                  const float da = static_cast<float>(dst_px.a) / 255.0F;
                  const float out_a = sa + da * (1.0F - sa);
                  if (out_a > 0.0F) {
                    const float inv_sa = 1.0F - sa;
                    dst_px.r = static_cast<std::uint8_t>(std::clamp(
                        std::lround((src_px.r * sa + dst_px.r * da * inv_sa) / out_a), 0L, 255L));
                    dst_px.g = static_cast<std::uint8_t>(std::clamp(
                        std::lround((src_px.g * sa + dst_px.g * da * inv_sa) / out_a), 0L, 255L));
                    dst_px.b = static_cast<std::uint8_t>(std::clamp(
                        std::lround((src_px.b * sa + dst_px.b * da * inv_sa) / out_a), 0L, 255L));
                    dst_px.a = static_cast<std::uint8_t>(std::clamp(std::lround(out_a * 255.0F), 0L, 255L));
                  }
                }
              }
              tile_modified = true;
            }
          }
          if (tile_modified) {
            img.mark_tile_dirty(tx, ty);
          }
        }
      }
    }
    selection_.translate(floating_selection_.offset_x, floating_selection_.offset_y);
  } else {
    render_transformed_floating_selection(floating_selection_, img, width_, height_);
    selection_.transform(floating_selection_.pivot_x, floating_selection_.pivot_y,
                         floating_selection_.offset_x, floating_selection_.offset_y,
                         floating_selection_.rotation, floating_selection_.scale_x,
                         floating_selection_.scale_y);
  }

  floating_selection_.active = false;
  floating_selection_.pixels.clear();
  floating_selection_.mask.clear();
  touch_composite();
  commit_change();
}

void Document::cancel_move_selection() {
  if (!floating_selection_.active) {
    return;
  }
  auto &img = active_layer().image();
  for (int y = 0; y < floating_selection_.height; ++y) {
    const int doc_y = floating_selection_.origin_y + y;
    if (doc_y < 0 || doc_y >= height_) continue;
    for (int x = 0; x < floating_selection_.width; ++x) {
      const int doc_x = floating_selection_.origin_x + x;
      if (doc_x < 0 || doc_x >= width_) continue;
      const std::size_t idx = static_cast<std::size_t>(y) * floating_selection_.width + x;
      if (floating_selection_.mask[idx] > 0 && floating_selection_.pixels[idx].a > 0) {
        img.blend_pixel(doc_x, doc_y, floating_selection_.pixels[idx]);
      }
    }
  }

  floating_selection_.active = false;
  floating_selection_.pixels.clear();
  floating_selection_.mask.clear();
  touch_composite();
  cancel_change();
}

void Document::flip_floating_horizontal() {
  if (!floating_selection_.active || floating_selection_.width <= 0 || floating_selection_.height <= 0) {
    return;
  }
  const int w = floating_selection_.width;
  const int h = floating_selection_.height;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w / 2; ++x) {
      const int x2 = w - 1 - x;
      const std::size_t idx1 = static_cast<std::size_t>(y) * w + x;
      const std::size_t idx2 = static_cast<std::size_t>(y) * w + x2;
      std::swap(floating_selection_.pixels[idx1], floating_selection_.pixels[idx2]);
      if (!floating_selection_.mask.empty()) {
        std::swap(floating_selection_.mask[idx1], floating_selection_.mask[idx2]);
      }
    }
  }
  touch_composite();
}

void Document::flip_floating_vertical() {
  if (!floating_selection_.active || floating_selection_.width <= 0 || floating_selection_.height <= 0) {
    return;
  }
  const int w = floating_selection_.width;
  const int h = floating_selection_.height;
  for (int y = 0; y < h / 2; ++y) {
    const int y2 = h - 1 - y;
    for (int x = 0; x < w; ++x) {
      const std::size_t idx1 = static_cast<std::size_t>(y) * w + x;
      const std::size_t idx2 = static_cast<std::size_t>(y2) * w + x;
      std::swap(floating_selection_.pixels[idx1], floating_selection_.pixels[idx2]);
      if (!floating_selection_.mask.empty()) {
        std::swap(floating_selection_.mask[idx1], floating_selection_.mask[idx2]);
      }
    }
  }
  touch_composite();
}

void Document::nudge_floating(int dx, int dy) {
  if (!floating_selection_.active) return;
  floating_selection_.offset_x += dx;
  floating_selection_.offset_y += dy;
  touch_composite();
}

void Document::set_clipboard(int w, int h, std::vector<Rgba8> pixels, std::vector<std::uint8_t> mask) {
  clipboard_.width = w;
  clipboard_.height = h;
  clipboard_.pixels = std::move(pixels);
  clipboard_.mask = std::move(mask);
  clipboard_.valid = (w > 0 && h > 0 && !clipboard_.pixels.empty());
}

void Document::copy_selection() {
  if (floating_selection_.active) {
    clipboard_.width = floating_selection_.width;
    clipboard_.height = floating_selection_.height;
    clipboard_.pixels = floating_selection_.pixels;
    clipboard_.mask = floating_selection_.mask;
    clipboard_.valid = true;
    return;
  }

  if (!has_active_layer()) {
    return;
  }

  int cx = 0, cy = 0, cw = 0, ch = 0;
  if (selection_.has_selection()) {
    const auto &b = selection_.bounds();
    cx = b.x;
    cy = b.y;
    cw = b.width;
    ch = b.height;
  } else {
    const auto bounds = active_layer().image().content_bounds();
    if (bounds.has_value()) {
      cx = bounds->x;
      cy = bounds->y;
      cw = bounds->width;
      ch = bounds->height;
    } else {
      cx = 0;
      cy = 0;
      cw = width_;
      ch = height_;
    }
  }

  if (cw <= 0 || ch <= 0) {
    return;
  }

  clipboard_.width = cw;
  clipboard_.height = ch;
  clipboard_.pixels.assign(static_cast<std::size_t>(cw) * ch, Rgba8{0, 0, 0, 0});
  clipboard_.mask.assign(static_cast<std::size_t>(cw) * ch, 0);

  const auto &img = active_layer().image();
  for (int y = 0; y < ch; ++y) {
    const int doc_y = cy + y;
    if (doc_y < 0 || doc_y >= height_) continue;
    for (int x = 0; x < cw; ++x) {
      const int doc_x = cx + x;
      if (doc_x < 0 || doc_x >= width_) continue;
      const std::size_t idx = static_cast<std::size_t>(y) * cw + x;
      const std::uint8_t m = selection_.has_selection() ? selection_.value(doc_x, doc_y) : 255;
      if (m > 0) {
        clipboard_.pixels[idx] = img.pixel(doc_x, doc_y);
        clipboard_.mask[idx] = m;
      }
    }
  }
  clipboard_.valid = true;
}

void Document::cut_selection() {
  copy_selection();
  if (floating_selection_.active) {
    floating_selection_.active = false;
    floating_selection_.pixels.clear();
    floating_selection_.mask.clear();
    touch_composite();
    commit_change();
  } else {
    clear_selection_content();
  }
}

bool Document::paste_selection() {
  if (!clipboard_.valid || clipboard_.width <= 0 || clipboard_.height <= 0) {
    return false;
  }
  if (floating_selection_.active) {
    commit_move_selection();
  }
  begin_change();
  floating_selection_.active = true;
  int target_x = 0;
  int target_y = 0;
  if (selection_.has_selection()) {
    target_x = selection_.bounds().x;
    target_y = selection_.bounds().y;
  } else {
    target_x = (width_ - clipboard_.width) / 2;
    target_y = (height_ - clipboard_.height) / 2;
  }
  floating_selection_.origin_x = target_x;
  floating_selection_.origin_y = target_y;
  floating_selection_.offset_x = 0;
  floating_selection_.offset_y = 0;
  floating_selection_.width = clipboard_.width;
  floating_selection_.height = clipboard_.height;
  floating_selection_.rotation = 0.0;
  floating_selection_.scale_x = 1.0;
  floating_selection_.scale_y = 1.0;
  floating_selection_.pivot_x = target_x + clipboard_.width / 2.0;
  floating_selection_.pivot_y = target_y + clipboard_.height / 2.0;
  floating_selection_.pixels = clipboard_.pixels;
  floating_selection_.mask = clipboard_.mask;
  if (floating_selection_.mask.empty()) {
    floating_selection_.mask.assign(static_cast<std::size_t>(clipboard_.width) * clipboard_.height, 255);
  }
  selection_.set_rect(target_x, target_y, clipboard_.width, clipboard_.height);
  touch_composite();
  modified_ = true;
  return true;
}

bool Document::paste_as_new_layer() {
  if (!clipboard_.valid || clipboard_.width <= 0 || clipboard_.height <= 0) {
    return false;
  }
  if (floating_selection_.active) {
    commit_move_selection();
  }
  begin_change();
  std::size_t new_idx = add_layer("Pasted Layer");
  set_active_layer_index(new_idx);
  auto &img = active_layer().image();
  int target_x = (width_ - clipboard_.width) / 2;
  int target_y = (height_ - clipboard_.height) / 2;
  if (selection_.has_selection()) {
    target_x = selection_.bounds().x;
    target_y = selection_.bounds().y;
  }
  for (int y = 0; y < clipboard_.height; ++y) {
    const int doc_y = target_y + y;
    if (doc_y < 0 || doc_y >= height_) continue;
    for (int x = 0; x < clipboard_.width; ++x) {
      const int doc_x = target_x + x;
      if (doc_x < 0 || doc_x >= width_) continue;
      const std::size_t idx = static_cast<std::size_t>(y) * clipboard_.width + x;
      const std::uint8_t m = (!clipboard_.mask.empty()) ? clipboard_.mask[idx] : 255;
      if (m > 0 && clipboard_.pixels[idx].a > 0) {
        Rgba8 px = clipboard_.pixels[idx];
        if (m < 255) {
          px.a = static_cast<std::uint8_t>((static_cast<int>(px.a) * m) / 255);
        }
        img.set_pixel(doc_x, doc_y, px);
      }
    }
  }
  selection_.set_rect(target_x, target_y, clipboard_.width, clipboard_.height);
  touch_composite();
  modified_ = true;
  commit_change();
  return true;
}

void Document::move_selection(int delta_x, int delta_y) {
  if (delta_x == 0 && delta_y == 0) {
    return;
  }
  if (!selection_.active()) {
    begin_change();
    active_layer().image().resize(width_, height_, delta_x, delta_y);
    touch_composite();
    commit_change();
  } else {
    start_move_selection();
    update_move_selection(delta_x, delta_y);
    commit_move_selection();
  }
}
void Document::flood_fill(int start_x, int start_y, Rgba8 fill_color, int tolerance) {
  if (start_x < 0 || start_y < 0 || start_x >= width_ || start_y >= height_) {
    return;
  }
  if (selection_.active() && !selection_.is_selected(start_x, start_y)) {
    return;
  }

  auto &img = active_layer().image();
  if (img.alpha_locked() && img.pixel(start_x, start_y).a == 0) {
    return;
  }

  const Rgba8 origin = img.pixel(start_x, start_y);
  if (origin.r == fill_color.r && origin.g == fill_color.g && origin.b == fill_color.b &&
      origin.a == fill_color.a) {
    return;
  }

  begin_change();

  const int cols = img.tile_columns();
  const int rows = img.tile_rows();
  std::vector<const Tile *> read_tiles(static_cast<std::size_t>(cols) * rows, nullptr);
  std::vector<Tile *> write_tiles(static_cast<std::size_t>(cols) * rows, nullptr);

  for (int ty = 0; ty < rows; ++ty) {
    for (int tx = 0; tx < cols; ++tx) {
      read_tiles[static_cast<std::size_t>(ty) * cols + tx] = img.tile_at(tx, ty);
    }
  }

  constexpr Rgba8 kTransparent{0, 0, 0, 0};
  auto get_pixel = [&](int x, int y) -> Rgba8 {
    const int tx = x / kTileSize;
    const int ty = y / kTileSize;
    const Tile *t = read_tiles[static_cast<std::size_t>(ty) * cols + tx];
    if (!t) return kTransparent;
    return t->pixels[static_cast<std::size_t>(y % kTileSize) * kTileSize + (x % kTileSize)];
  };

  auto color_matches = [&](Rgba8 c) -> bool {
    return std::abs(static_cast<int>(c.r) - origin.r) <= tolerance &&
           std::abs(static_cast<int>(c.g) - origin.g) <= tolerance &&
           std::abs(static_cast<int>(c.b) - origin.b) <= tolerance &&
           std::abs(static_cast<int>(c.a) - origin.a) <= tolerance;
  };

  const bool has_selection = selection_.active();
  const auto &sel_data = selection_.data();
  auto is_pixel_selected = [&](int x, int y) -> bool {
    if (!has_selection) return true;
    if (sel_data.empty()) return true;
    return sel_data[static_cast<std::size_t>(y) * width_ + x] > 0;
  };

  const bool alpha_lock = img.alpha_locked();
  auto set_pixel = [&](int x, int y) {
    const int tx = x / kTileSize;
    const int ty = y / kTileSize;
    const std::size_t tidx = static_cast<std::size_t>(ty) * cols + tx;
    Tile *wt = write_tiles[tidx];
    if (!wt) {
      wt = img.writable_tile_at(tx, ty);
      write_tiles[tidx] = wt;
      read_tiles[tidx] = wt;
      img.mark_tile_dirty(tx, ty);
    }
    Rgba8 px_color = fill_color;
    if (alpha_lock) {
      const auto orig_a = wt->pixels[static_cast<std::size_t>(y % kTileSize) * kTileSize + (x % kTileSize)].a;
      if (orig_a == 0) return;
      px_color.a = orig_a;
    }
    wt->pixels[static_cast<std::size_t>(y % kTileSize) * kTileSize + (x % kTileSize)] = px_color;
  };

  std::vector<uint8_t> visited(static_cast<std::size_t>(width_) * height_, 0);

  struct Segment {
    int y;
    int x1;
    int x2;
    int dy;
  };
  std::vector<Segment> stack;
  stack.reserve(4096);

  int x_l = start_x;
  while (x_l >= 0 && is_pixel_selected(x_l, start_y) && color_matches(get_pixel(x_l, start_y))) {
    visited[static_cast<std::size_t>(start_y) * width_ + x_l] = 1;
    set_pixel(x_l, start_y);
    --x_l;
  }
  ++x_l;

  int x_r = start_x + 1;
  while (x_r < width_ && is_pixel_selected(x_r, start_y) && color_matches(get_pixel(x_r, start_y))) {
    visited[static_cast<std::size_t>(start_y) * width_ + x_r] = 1;
    set_pixel(x_r, start_y);
    ++x_r;
  }
  --x_r;

  if (start_y > 0) stack.push_back({start_y - 1, x_l, x_r, -1});
  if (start_y < height_ - 1) stack.push_back({start_y + 1, x_l, x_r, 1});

  while (!stack.empty()) {
    Segment seg = stack.back();
    stack.pop_back();

    const int y = seg.y;
    int x = seg.x1;

    while (x <= seg.x2) {
      const std::size_t idx = static_cast<std::size_t>(y) * width_ + x;
      if (!visited[idx] && is_pixel_selected(x, y) && color_matches(get_pixel(x, y))) {
        int l = x;
        while (l >= 0 && !visited[static_cast<std::size_t>(y) * width_ + l] &&
               is_pixel_selected(l, y) && color_matches(get_pixel(l, y))) {
          visited[static_cast<std::size_t>(y) * width_ + l] = 1;
          set_pixel(l, y);
          --l;
        }
        ++l;

        int r = x + 1;
        while (r < width_ && !visited[static_cast<std::size_t>(y) * width_ + r] &&
               is_pixel_selected(r, y) && color_matches(get_pixel(r, y))) {
          visited[static_cast<std::size_t>(y) * width_ + r] = 1;
          set_pixel(r, y);
          ++r;
        }
        --r;

        if (y + seg.dy >= 0 && y + seg.dy < height_) {
          stack.push_back({y + seg.dy, l, r, seg.dy});
        }
        if (y - seg.dy >= 0 && y - seg.dy < height_) {
          stack.push_back({y - seg.dy, l, r, -seg.dy});
        }
        x = r + 1;
      } else {
        ++x;
      }
    }
  }

  touch_composite();
  commit_change();
}

void Document::flood_select(int start_x, int start_y, int tolerance, bool contiguous, SelectionCombineMode mode) {
  if (start_x < 0 || start_y < 0 || start_x >= width_ || start_y >= height_) {
    return;
  }
  if (!has_active_layer()) {
    auto_select_top_layer();
  }
  if (!has_active_layer()) {
    return;
  }

  auto &img = active_layer().image();
  const Rgba8 origin = img.pixel(start_x, start_y);

  auto color_matches = [&](Rgba8 c) -> bool {
    const int diff = std::abs(static_cast<int>(c.r) - origin.r) +
                     std::abs(static_cast<int>(c.g) - origin.g) +
                     std::abs(static_cast<int>(c.b) - origin.b) +
                     std::abs(static_cast<int>(c.a) - origin.a);
    return diff <= tolerance * 4;
  };

  std::vector<std::uint8_t> transient(static_cast<std::size_t>(width_) * height_, 0);
  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  if (!contiguous) {
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        if (color_matches(img.pixel(x, y))) {
          transient[static_cast<std::size_t>(y) * width_ + x] = 255;
          min_x = std::min(min_x, x);
          max_x = std::max(max_x, x);
          min_y = std::min(min_y, y);
          max_y = std::max(max_y, y);
        }
      }
    }
  } else {
    std::vector<std::pair<int, int>> queue;
    queue.push_back({start_x, start_y});
    transient[static_cast<std::size_t>(start_y) * width_ + start_x] = 255;
    min_x = start_x;
    max_x = start_x;
    min_y = start_y;
    max_y = start_y;

    std::size_t head = 0;
    while (head < queue.size()) {
      const auto [cx, cy] = queue[head++];
      constexpr int dx[4] = {-1, 1, 0, 0};
      constexpr int dy[4] = {0, 0, -1, 1};
      for (int i = 0; i < 4; ++i) {
        const int nx = cx + dx[i];
        const int ny = cy + dy[i];
        if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
          const std::size_t n_idx = static_cast<std::size_t>(ny) * width_ + nx;
          if (transient[n_idx] == 0 && color_matches(img.pixel(nx, ny))) {
            transient[n_idx] = 255;
            queue.push_back({nx, ny});
            min_x = std::min(min_x, nx);
            max_x = std::max(max_x, nx);
            min_y = std::min(min_y, ny);
            max_y = std::max(max_y, ny);
          }
        }
      }
    }
  }

  SelectionRect incoming_bounds{};
  if (max_x >= min_x && max_y >= min_y) {
    incoming_bounds = SelectionRect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
  }

  begin_change();
  selection_.apply_combine_mask(transient, incoming_bounds, mode);
  commit_change();
  touch_composite();
}

void Document::feather_selection(double radius) {
  if (!selection_.has_selection()) return;
  begin_change();
  selection_.feather(radius);
  commit_change();
  touch_composite();
}

void Document::grow_selection(int pixels) {
  if (!selection_.has_selection()) return;
  begin_change();
  selection_.grow(pixels);
  commit_change();
  touch_composite();
}

void Document::shrink_selection(int pixels) {
  if (!selection_.has_selection()) return;
  begin_change();
  selection_.shrink(pixels);
  commit_change();
  touch_composite();
}

void Document::border_selection(int pixels) {
  if (!selection_.has_selection()) return;
  begin_change();
  selection_.border(pixels);
  commit_change();
  touch_composite();
}





void Document::set_active_layer_index(std::size_t index) {
  has_active_layer_ = true;
  if (layers_.empty()) {
    active_layer_index_ = 0;
  } else {
    active_layer_index_ = std::min(index, layers_.size() - 1);
  }
}

Layer &Document::active_layer() {
  if (!has_active_layer_) auto_select_top_layer();
  return layers_.at(active_layer_index_);
}

const Layer &Document::active_layer() const {
  if (!has_active_layer_) const_cast<Document *>(this)->auto_select_top_layer();
  return layers_.at(active_layer_index_);
}

std::size_t Document::auto_select_top_layer() {
  if (layers_.empty()) {
    has_active_layer_ = false;
    return 0;
  }
  for (std::size_t i = layers_.size(); i > 0; --i) {
    const std::size_t idx = i - 1;
    if (!layers_[idx].is_group()) {
      set_active_layer_index(idx);
      return idx;
    }
  }
  set_active_layer_index(layers_.size() - 1);
  return layers_.size() - 1;
}

std::optional<std::size_t> Document::find_layer_at(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    return std::nullopt;
  }
  for (std::size_t i = layers_.size(); i > 0; --i) {
    const std::size_t idx = i - 1;
    const auto &l = layers_[idx];
    if (!l.visible() || l.is_group() || l.opacity() <= 0.0) {
      continue;
    }
    bool ancestor_hidden = false;
    std::size_t pid = l.parent_id();
    std::size_t guard = 0;
    while (pid != 0 && guard++ < layers_.size()) {
      const Layer *p = find_layer_by_id(pid);
      if (!p) break;
      if (!p->visible() || p->opacity() <= 0.0) {
        ancestor_hidden = true;
        break;
      }
      pid = p->parent_id();
    }
    if (ancestor_hidden) {
      continue;
    }

    if (l.image().pixel(x, y).a > 0) {
      return idx;
    }
  }
  return std::nullopt;
}

std::size_t Document::add_layer(std::string name, bool fill_white) {
  if (name.empty()) {
    name = "Layer " + std::to_string(layers_.size() + 1);
  }
  Layer layer(std::move(name), width_, height_, fill_white);
  layer.set_id(++next_layer_id_);
  const std::size_t insert_pos =
      layers_.empty() ? 0 : std::min(active_layer_index_ + 1, layers_.size());
  layers_.insert(layers_.begin() + insert_pos, std::move(layer));
  active_layer_index_ = insert_pos;
  has_active_layer_ = true;
  touch_composite();
  modified_ = true;
  return active_layer_index_;
}

std::size_t Document::add_group(std::string name) {
  if (name.empty()) {
    int folder_count = 0;
    for (const auto &l : layers_) {
      if (l.is_group()) ++folder_count;
    }
    name = "Folder " + std::to_string(folder_count + 1);
  }
  Layer layer(std::move(name), width_, height_, false);
  layer.set_is_group(true);
  layer.set_id(++next_layer_id_);
  layer.set_expanded(true);
  const std::size_t insert_pos =
      layers_.empty() ? 0 : std::min(active_layer_index_ + 1, layers_.size());
  layers_.insert(layers_.begin() + insert_pos, std::move(layer));
  active_layer_index_ = insert_pos;
  has_active_layer_ = true;
  touch_composite();
  modified_ = true;
  return active_layer_index_;
}

void Document::rename_layer(std::size_t index, std::string new_name) {
  if (index < layers_.size() && !new_name.empty()) {
    layers_[index].set_name(std::move(new_name));
    modified_ = true;
  }
}
const Layer *Document::find_layer_by_id(std::size_t id) const {
  if (id == 0) return nullptr;
  for (const auto &l : layers_) {
    if (l.id() == id) return &l;
  }
  return nullptr;
}

Layer *Document::find_layer_by_id(std::size_t id) {
  if (id == 0) return nullptr;
  for (auto &l : layers_) {
    if (l.id() == id) return &l;
  }
  return nullptr;
}
int Document::get_layer_depth(std::size_t index) const {
  if (index >= layers_.size()) return 0;
  int depth = 0;
  std::size_t pid = layers_[index].parent_id();
  std::size_t guard = 0;
  while (pid != 0 && guard++ < layers_.size()) {
    const Layer *p = find_layer_by_id(pid);
    if (!p || !p->is_group()) break;
    depth++;
    pid = p->parent_id();
  }
  return depth;
}

bool Document::is_ancestor_collapsed(std::size_t index) const {
  if (index >= layers_.size()) return false;
  std::size_t pid = layers_[index].parent_id();
  std::size_t guard = 0;
  while (pid != 0 && guard++ < layers_.size()) {
    const Layer *p = find_layer_by_id(pid);
    if (!p) break;
    if (!p->expanded()) return true;
    pid = p->parent_id();
  }
  return false;
}

bool Document::is_descendant_of(std::size_t layer_id, std::size_t group_id) const {
  if (layer_id == 0 || group_id == 0) return false;
  const Layer *l = find_layer_by_id(layer_id);
  if (!l) return false;
  std::size_t pid = l->parent_id();
  std::size_t guard = 0;
  while (pid != 0 && guard++ < layers_.size()) {
    if (pid == group_id) return true;
    const Layer *p = find_layer_by_id(pid);
    if (!p) break;
    pid = p->parent_id();
  }
  return false;
}

std::pair<std::size_t, std::size_t> Document::layer_block_range(std::size_t index) const {
  if (index >= layers_.size()) return {index, index};
  std::size_t lowest = index;
  const std::size_t owner_id = layers_[index].id();
  for (std::size_t i = index; i > 0; --i) {
    if (is_descendant_of(layers_[i - 1].id(), owner_id)) {
      lowest = i - 1;
    } else {
      break;
    }
  }
  return {lowest, index};
}

void Document::clean_hierarchy_invariants() {
  if (layers_.size() <= 1) return;

  // Ensure parent exists, is a group, and sits above child in the array
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    std::size_t pid = layers_[i].parent_id();
    if (pid != 0) {
      bool found_parent_above = false;
      for (std::size_t j = i + 1; j < layers_.size(); ++j) {
        if (layers_[j].id() == pid && layers_[j].is_group()) {
          found_parent_above = true;
          break;
        }
      }
      if (!found_parent_above) {
        layers_[i].set_parent_id(0);
      }
    }
  }
}



void Document::insert_layer(std::size_t index, Layer layer) {
  const std::size_t insert_pos = std::min(index, layers_.size());
  layers_.insert(layers_.begin() + insert_pos, std::move(layer));
  if (active_layer_index_ >= insert_pos) {
    active_layer_index_ = std::min(active_layer_index_ + 1, layers_.size() - 1);
  }
  touch_composite();
  modified_ = true;
}

bool Document::remove_layer(std::size_t index) {
  if (index >= layers_.size() || layers_.size() <= 1) {
    return false;
  }
  // Safety: detach children of a removed group so their parent_id never dangles.
  if (layers_[index].is_group()) {
    const std::size_t gid = layers_[index].id();
    for (auto &l : layers_) {
      if (l.parent_id() == gid) {
        l.set_parent_id(0);
      }
    }
  }
  layers_.erase(layers_.begin() + index);
  if (active_layer_index_ >= layers_.size()) {
    active_layer_index_ = layers_.size() - 1;
  } else if (active_layer_index_ > index) {
    --active_layer_index_;
  }
  touch_composite();
  modified_ = true;
  return true;
}

bool Document::move_layer(std::size_t from_index, std::size_t to_index) {
  if (from_index >= layers_.size() || to_index >= layers_.size() || from_index == to_index) {
    return false;
  }

  if (layers_[from_index].is_group()) {
    const std::size_t gid = layers_[from_index].id();
    std::size_t lowest = from_index;
    for (std::size_t i = from_index; i > 0; --i) {
      if (is_descendant_of(layers_[i - 1].id(), gid)) {
        lowest = i - 1;
      } else {
        break;
      }
    }
    const std::size_t K = from_index - lowest + 1;
    if (K > 1) {
      std::vector<Layer> block;
      block.reserve(K);
      for (std::size_t i = 0; i < K; ++i) {
        block.push_back(std::move(layers_[lowest + i]));
      }
      layers_.erase(layers_.begin() + lowest, layers_.begin() + lowest + K);
      std::size_t insert_pos = (to_index > from_index) ? (to_index + 1 - K) : to_index;
      insert_pos = std::min(insert_pos, layers_.size());
      for (std::size_t i = 0; i < K; ++i) {
        layers_.insert(layers_.begin() + insert_pos + i, std::move(block[i]));
      }
      active_layer_index_ = insert_pos + K - 1;
      clean_hierarchy_invariants();
      touch_composite();
      modified_ = true;
      return true;
    }
  }

  Layer layer = std::move(layers_[from_index]);
  layers_.erase(layers_.begin() + from_index);
  layers_.insert(layers_.begin() + to_index, std::move(layer));
  active_layer_index_ = to_index;
  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  return true;
}
bool Document::move_layer_relative(std::size_t from_index, std::size_t target_index, bool below) {
  if (from_index >= layers_.size() || target_index >= layers_.size() || from_index == target_index) {
    return false;
  }

  const auto [f0, f1] = layer_block_range(from_index);
  const auto [t0, t1] = layer_block_range(target_index);
  const std::size_t K = f1 - f0 + 1;

  // Moving inside one's own block is meaningless; caller forbids it anyway.
  if (t0 >= f0 && t1 <= f1) {
    return false;
  }

  // Extract the moving block (layer + its descendants keep their internal order)
  std::vector<Layer> block;
  block.reserve(K);
  for (std::size_t i = 0; i < K; ++i) {
    block.push_back(std::move(layers_[f0 + i]));
  }
  layers_.erase(layers_.begin() + f0, layers_.begin() + f1 + 1);

  // Adjust target block indices for the removed range
  std::size_t t0a = t0;
  std::size_t t1a = t1;
  if (t0 > f1) {
    t0a -= K;
    t1a -= K;
  }

  const std::size_t insert_pos = below ? t0a : (t1a + 1);
  for (std::size_t i = 0; i < K; ++i) {
    layers_.insert(layers_.begin() + insert_pos + i, std::move(block[i]));
  }

  active_layer_index_ = insert_pos + (K - 1);
  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  return true;
}

bool Document::move_into_group(std::size_t layer_idx, std::size_t group_idx) {
  if (layer_idx >= layers_.size() || group_idx >= layers_.size() || layer_idx == group_idx) {
    return false;
  }
  if (!layers_[group_idx].is_group()) {
    return false;
  }

  // Prevent circular parenting: target folder must not be a descendant of the moving item.
  const std::size_t moving_id = layers_[layer_idx].id();
  const std::size_t target_folder_id = layers_[group_idx].id();
  if (is_descendant_of(target_folder_id, moving_id)) {
    return false;
  }

  // Move the whole block (layer + its descendants) so nested groups keep their children.
  const auto [b0, b1] = layer_block_range(layer_idx);
  const std::size_t K = b1 - b0 + 1;

  std::vector<Layer> block;
  block.reserve(K);
  for (std::size_t i = 0; i < K; ++i) {
    block.push_back(std::move(layers_[b0 + i]));
  }
  layers_.erase(layers_.begin() + b0, layers_.begin() + b1 + 1);

  // Locate the target folder after the extraction
  std::size_t folder_idx = 0;
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (layers_[i].id() == target_folder_id) {
      folder_idx = i;
      break;
    }
  }

  // Re-insert directly below the folder: becomes its topmost child (parent must sit above).
  // The moving root is the TOP of the extracted block (block is ordered bottom -> top).
  block.back().set_parent_id(target_folder_id);
  for (std::size_t i = 0; i < K; ++i) {
    layers_.insert(layers_.begin() + folder_idx + i, std::move(block[i]));
  }

  // Expand target folder (index-safe via id lookup)
  if (Layer *folder = find_layer_by_id(target_folder_id)) {
    folder->set_expanded(true);
  }
  active_layer_index_ = folder_idx;

  has_active_layer_ = true;
  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  return true;
}


bool Document::duplicate_layer(std::size_t index) {
  if (index >= layers_.size()) {
    return false;
  }
  const auto &src = layers_[index];
  Layer dup(src.name() + " copy", width_, height_);
  dup.set_visible(src.visible());
  dup.set_opacity(src.opacity());
  dup.set_composite_op(src.composite_op());
  dup.set_locked(src.locked());
  dup.set_alpha_locked(src.alpha_locked());
  dup.set_clipping_mask(src.clipping_mask());
  dup.set_screentone(src.screentone());
  dup.set_screentone_frequency(src.screentone_frequency());
  dup.set_screentone_angle(src.screentone_angle());
  dup.set_screentone_pattern(src.screentone_pattern());
  // Same-parent duplicates stay inside the same group (parent must sit above the copy).
  if (!src.is_group() && src.parent_id() != 0) {
    const std::size_t pid = src.parent_id();
    bool parent_above = false;
    for (std::size_t j = index + 1; j < layers_.size(); ++j) {
      if (layers_[j].id() == pid && layers_[j].is_group()) {
        parent_above = true;
        break;
      }
    }
    if (parent_above) {
      dup.set_parent_id(pid);
    }
  }
  dup.image().restore(src.image().snapshot());

  const std::size_t insert_pos = index + 1;
  layers_.insert(layers_.begin() + insert_pos, std::move(dup));
  active_layer_index_ = insert_pos;
  touch_composite();
  modified_ = true;
  return true;
}
bool Document::merge_layer_down(std::size_t index) {
  if (index == 0 || index >= layers_.size()) {
    return false;
  }
  if (layers_[index].is_group()) {
    return merge_group(index);
  }

  begin_change();

  auto &src_layer = layers_[index];
  auto &dst_layer = layers_[index - 1];

  const double opacity = src_layer.opacity();
  const BlendOp op = blend_op_from_string(src_layer.composite_op());
  const bool is_clipped = src_layer.clipping_mask();
  const bool is_screentone = src_layer.screentone();
  const double st_freq = src_layer.screentone_frequency();
  const double st_angle = src_layer.screentone_angle();
  const double st_cos = std::cos(st_angle * std::numbers::pi / 180.0);
  const double st_sin = std::sin(st_angle * std::numbers::pi / 180.0);
  const auto st_pat = src_layer.screentone_pattern();

  for (const auto &[key, src_tile] : src_layer.image().tiles()) {
    const int tx = TiledImage::key_x(key);
    const int ty = TiledImage::key_y(key);

    for (int ly = 0; ly < kTileSize; ++ly) {
      const int gy = ty * kTileSize + ly;
      if (gy >= height_) continue;
      for (int lx = 0; lx < kTileSize; ++lx) {
        const int gx = tx * kTileSize + lx;
        if (gx >= width_) continue;

        const std::size_t p_idx = static_cast<std::size_t>(ly) * kTileSize + lx;
        Rgba8 src_px = src_tile->pixels[p_idx];
        if (src_px.a == 0) continue;

        double clip = 1.0;
        if (is_clipped) {
          clip = dst_layer.image().pixel(gx, gy).a / 255.0;
          if (clip <= 0.001) continue;
        }

        if (is_screentone) {
          src_px = apply_screentone(src_px, gx, gy, st_freq, st_cos, st_sin, st_pat);
          if (src_px.a == 0) continue;
        }

        Rgba8 dst_px = dst_layer.image().pixel(gx, gy);
        blend_layer_pixel(dst_px, src_px, opacity, op, clip);
        dst_layer.image().set_pixel(gx, gy, dst_px);
      }
    }
  }

  layers_.erase(layers_.begin() + index);
  if (active_layer_index_ >= layers_.size() || active_layer_index_ == index) {
    active_layer_index_ = index - 1;
  } else if (active_layer_index_ > index) {
    --active_layer_index_;
  }

  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  commit_change();
  return true;
}

bool Document::merge_group(std::size_t folder_index) {
  if (folder_index >= layers_.size() || !layers_[folder_index].is_group()) {
    return false;
  }

  begin_change();

  const std::size_t folder_id = layers_[folder_index].id();
  const std::string folder_name = layers_[folder_index].name();
  const double folder_opacity = layers_[folder_index].opacity();
  const std::string folder_op = layers_[folder_index].composite_op();
  const bool folder_clip = layers_[folder_index].clipping_mask();
  const std::size_t folder_pid = layers_[folder_index].parent_id();

  std::vector<std::size_t> child_indices;
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (i == folder_index) continue;
    std::size_t pid = layers_[i].parent_id();
    std::size_t guard = 0;
    while (pid != 0 && guard++ < layers_.size()) {
      if (pid == folder_id) {
        child_indices.push_back(i);
        break;
      }
      const Layer *p = find_layer_by_id(pid);
      if (!p) break;
      pid = p->parent_id();
    }
  }

  TiledImage merged_img(width_, height_);
  std::sort(child_indices.begin(), child_indices.end());

  for (std::size_t c_idx : child_indices) {
    const auto &cl = layers_[c_idx];
    if (cl.is_group() || !cl.visible() || cl.opacity() <= 0.0) continue;
    const BlendOp op = blend_op_from_string(cl.composite_op());
    const double st_cos = std::cos(cl.screentone_angle() * std::numbers::pi / 180.0);
    const double st_sin = std::sin(cl.screentone_angle() * std::numbers::pi / 180.0);

    for (const auto &[key, src_tile] : cl.image().tiles()) {
      const int tx = TiledImage::key_x(key);
      const int ty = TiledImage::key_y(key);
      for (int ly = 0; ly < kTileSize; ++ly) {
        const int gy = ty * kTileSize + ly;
        if (gy >= height_) continue;
        for (int lx = 0; lx < kTileSize; ++lx) {
          const int gx = tx * kTileSize + lx;
          if (gx >= width_) continue;
          const std::size_t p_idx = static_cast<std::size_t>(ly) * kTileSize + lx;
          Rgba8 src_px = src_tile->pixels[p_idx];
          if (src_px.a == 0) continue;

          double clip = 1.0;
          if (cl.clipping_mask()) {
            clip = merged_img.pixel(gx, gy).a / 255.0;
            if (clip <= 0.001) continue;
          }

          if (cl.screentone()) {
            src_px = apply_screentone(src_px, gx, gy, cl.screentone_frequency(), st_cos, st_sin,
                                      cl.screentone_pattern());
            if (src_px.a == 0) continue;
          }

          Rgba8 dst_px = merged_img.pixel(gx, gy);
          blend_layer_pixel(dst_px, src_px, cl.opacity(), op, clip);
          merged_img.set_pixel(gx, gy, dst_px);
        }
      }
    }
  }

  Layer flattened(folder_name, width_, height_, false);
  flattened.set_id(folder_id);
  flattened.set_parent_id(folder_pid);
  flattened.set_opacity(folder_opacity);
  flattened.set_composite_op(folder_op);
  flattened.set_clipping_mask(folder_clip);
  flattened.image().restore(merged_img.snapshot());

  std::sort(child_indices.rbegin(), child_indices.rend());
  for (std::size_t del_idx : child_indices) {
    layers_.erase(layers_.begin() + del_idx);
    if (folder_index > del_idx) {
      --folder_index;
    }
  }

  layers_[folder_index] = std::move(flattened);
  active_layer_index_ = folder_index;

  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  commit_change();
  return true;
}
void Document::get_group_descendants_count(std::size_t folder_index,
                                           std::size_t &out_layer_count,
                                           std::size_t &out_folder_count) const {
  out_layer_count = 0;
  out_folder_count = 0;
  if (folder_index >= layers_.size() || !layers_[folder_index].is_group()) {
    return;
  }

  std::unordered_set<std::size_t> folder_ids;
  folder_ids.insert(layers_[folder_index].id());

  bool added = true;
  while (added) {
    added = false;
    for (const auto &l : layers_) {
      if (l.parent_id() != 0 && folder_ids.count(l.parent_id()) > 0) {
        if (l.is_group() && folder_ids.count(l.id()) == 0) {
          folder_ids.insert(l.id());
          added = true;
        }
      }
    }
  }

  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (i == folder_index) continue;
    if (layers_[i].parent_id() != 0 && folder_ids.count(layers_[i].parent_id()) > 0) {
      if (layers_[i].is_group()) {
        ++out_folder_count;
      } else {
        ++out_layer_count;
      }
    }
  }
}

bool Document::remove_group_recursive(std::size_t folder_index) {
  if (folder_index >= layers_.size() || !layers_[folder_index].is_group()) {
    return false;
  }

  std::unordered_set<std::size_t> folder_ids;
  folder_ids.insert(layers_[folder_index].id());

  bool added = true;
  while (added) {
    added = false;
    for (const auto &l : layers_) {
      if (l.parent_id() != 0 && folder_ids.count(l.parent_id()) > 0) {
        if (l.is_group() && folder_ids.count(l.id()) == 0) {
          folder_ids.insert(l.id());
          added = true;
        }
      }
    }
  }

  std::unordered_set<std::size_t> ids_to_remove;
  ids_to_remove.insert(layers_[folder_index].id());
  for (const auto &l : layers_) {
    if (l.parent_id() != 0 && folder_ids.count(l.parent_id()) > 0) {
      ids_to_remove.insert(l.id());
    }
  }

  begin_change();
  std::vector<Layer> remaining;
  for (std::size_t i = 0; i < layers_.size(); ++i) {
    if (ids_to_remove.count(layers_[i].id()) == 0) {
      remaining.push_back(std::move(layers_[i]));
    }
  }

  if (remaining.empty()) {
    remaining.emplace_back("1", width_, height_);
  }

  layers_ = std::move(remaining);
  if (active_layer_index_ >= layers_.size()) {
    active_layer_index_ = layers_.size() - 1;
  }

  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  commit_change();
  return true;
}


bool Document::merge_selected_layers(const std::vector<std::size_t> &indices) {
  if (indices.size() < 2) {
    return false;
  }
  std::vector<std::size_t> sorted_indices = indices;
  std::sort(sorted_indices.begin(), sorted_indices.end());

  begin_change();

  const std::size_t target_idx = sorted_indices.front();
  auto &dst_layer = layers_[target_idx];

  for (std::size_t k = 1; k < sorted_indices.size(); ++k) {
    const std::size_t s_idx = sorted_indices[k];
    if (s_idx >= layers_.size()) continue;
    const auto &src_layer = layers_[s_idx];
    if (src_layer.is_group()) continue;
    const BlendOp op = blend_op_from_string(src_layer.composite_op());

    for (const auto &[key, src_tile] : src_layer.image().tiles()) {
      const int tx = TiledImage::key_x(key);
      const int ty = TiledImage::key_y(key);
      for (int ly = 0; ly < kTileSize; ++ly) {
        const int gy = ty * kTileSize + ly;
        if (gy >= height_) continue;
        for (int lx = 0; lx < kTileSize; ++lx) {
          const int gx = tx * kTileSize + lx;
          if (gx >= width_) continue;
          const std::size_t p_idx = static_cast<std::size_t>(ly) * kTileSize + lx;
          Rgba8 src_px = src_tile->pixels[p_idx];
          if (src_px.a == 0) continue;

          double clip = 1.0;
          if (src_layer.clipping_mask()) {
            clip = dst_layer.image().pixel(gx, gy).a / 255.0;
            if (clip <= 0.001) continue;
          }

          Rgba8 dst_px = dst_layer.image().pixel(gx, gy);
          blend_layer_pixel(dst_px, src_px, src_layer.opacity(), op, clip);
          dst_layer.image().set_pixel(gx, gy, dst_px);
        }
      }
    }
  }

  for (std::size_t k = sorted_indices.size(); k > 1; --k) {
    const std::size_t del_idx = sorted_indices[k - 1];
    if (del_idx < layers_.size()) {
      layers_.erase(layers_.begin() + del_idx);
    }
  }

  active_layer_index_ = target_idx;
  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  commit_change();
  return true;
}

std::size_t Document::group_selected_layers(const std::vector<std::size_t> &indices, std::string group_name) {
  if (indices.empty()) {
    return add_group(group_name);
  }
  std::vector<std::size_t> sorted = indices;
  std::sort(sorted.begin(), sorted.end());

  begin_change();
  const std::size_t highest_orig = sorted.back();

  std::vector<Layer> selected_layers;
  for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
    std::size_t idx = *it;
    selected_layers.push_back(std::move(layers_[idx]));
    layers_.erase(layers_.begin() + idx);
  }
  std::reverse(selected_layers.begin(), selected_layers.end());

  const std::size_t insert_pos = std::min(highest_orig + 1 - sorted.size(), layers_.size());

  if (group_name.empty()) {
    int folder_count = 0;
    for (const auto &l : layers_) {
      if (l.is_group()) ++folder_count;
    }
    group_name = "Folder " + std::to_string(folder_count + 1);
  }
  Layer group(std::move(group_name), width_, height_, false);
  group.set_is_group(true);
  group.set_id(++next_layer_id_);
  group.set_expanded(true);
  const std::size_t gid = group.id();

  for (std::size_t i = 0; i < selected_layers.size(); ++i) {
    selected_layers[i].set_parent_id(gid);
    layers_.insert(layers_.begin() + insert_pos + i, std::move(selected_layers[i]));
  }
  const std::size_t group_idx = insert_pos + selected_layers.size();
  layers_.insert(layers_.begin() + group_idx, std::move(group));
  active_layer_index_ = group_idx;

  clean_hierarchy_invariants();
  touch_composite();
  modified_ = true;
  commit_change();
  return group_idx;
}


void Document::set_layer_visible(std::size_t index, bool visible) {
  if (index < layers_.size() && layers_[index].visible() != visible) {
    layers_[index].set_visible(visible);
    touch_composite();
    modified_ = true;
  }
}

void Document::set_layer_opacity(std::size_t index, double opacity) {
  if (index < layers_.size()) {
    layers_[index].set_opacity(opacity);
    touch_composite();
    modified_ = true;
  }
}
void Document::set_layer_alpha_locked(std::size_t index, bool locked) {
  if (index < layers_.size() && layers_[index].alpha_locked() != locked) {
    layers_[index].set_alpha_locked(locked);
    modified_ = true;
  }
}

void Document::set_layer_clipping_mask(std::size_t index, bool clipping) {
  if (index < layers_.size() && layers_[index].clipping_mask() != clipping) {
    layers_[index].set_clipping_mask(clipping);
    touch_composite();
    modified_ = true;
  }
}

void Document::set_layer_composite_op(std::size_t index, std::string op) {
  if (index < layers_.size() && layers_[index].composite_op() != op) {
    layers_[index].set_composite_op(std::move(op));
    touch_composite();
    modified_ = true;
  }
}

void Document::set_layer_screentone(std::size_t index, bool screentone, double freq, double angle) {
  if (index < layers_.size()) {
    layers_[index].set_screentone(screentone);
    layers_[index].set_screentone_frequency(freq);
    layers_[index].set_screentone_angle(angle);
    touch_composite();
    modified_ = true;
  }
}
void Document::set_layer_screentone_pattern(std::size_t index, ScreentonePattern pat) {
  if (index < layers_.size()) {
    layers_[index].set_screentone_pattern(pat);
    touch_composite();
    modified_ = true;
  }
}



void Document::update_composite() const {
  if (composite_.width() != width_ || composite_.height() != height_) {
    composite_ = TiledImage(width_, height_);
    composite_dirty_ = true;
  }
  if (!composite_dirty_ && composite_.tiles().empty()) {
    for (const auto &layer : layers_) {
      if (layer.visible() && layer.opacity() > 0.0 && !layer.image().tiles().empty()) {
        composite_dirty_ = true;
        break;
      }
    }
  }

  std::unordered_set<std::uint64_t> dirty_keys;
  if (composite_dirty_) {
    for (int ty = 0; ty < composite_.tile_rows(); ++ty) {
      for (int tx = 0; tx < composite_.tile_columns(); ++tx) {
        dirty_keys.insert(TiledImage::key_for(tx, ty));
      }
    }
  } else {
    for (const auto &layer : layers_) {
      for (std::uint64_t key : layer.image().dirty_tiles()) {
        dirty_keys.insert(key);
      }
    }
  }

  if (dirty_keys.empty()) {
    return;
  }

  for (std::uint64_t key : dirty_keys) {
    const int tx = TiledImage::key_x(key);
    const int ty = TiledImage::key_y(key);

    bool has_any_tile = false;
    for (const auto &layer : layers_) {
      if (layer.visible() && layer.opacity() > 0.0 && layer.image().tile_at(tx, ty) != nullptr) {
        has_any_tile = true;
        break;
      }
    }

    if (!has_any_tile) {
      composite_.remove_tile(tx, ty);
      continue;
    }

    Tile *comp_tile = composite_.writable_tile_at(tx, ty);
    composite_.mark_tile_dirty(tx, ty);
    std::fill(comp_tile->pixels.begin(), comp_tile->pixels.end(), Rgba8{0, 0, 0, 0});
    std::vector<std::uint8_t> base_alpha(kTileSize * kTileSize, 0);

    for (const auto &layer : layers_) {
      if (layer.is_group()) {
        continue;
      }

      bool ancestor_hidden = false;
      bool ancestor_clipped = false;
      std::size_t pid = layer.parent_id();
      std::size_t guard = 0;
      double parent_opacity = 1.0;
      while (pid != 0 && guard++ < layers_.size()) {
        const Layer *p = find_layer_by_id(pid);
        if (!p) break;
        if (!p->visible() || p->opacity() <= 0.0) {
          ancestor_hidden = true;
          break;
        }
        parent_opacity *= p->opacity();
        if (p->clipping_mask()) {
          ancestor_clipped = true;
        }
        pid = p->parent_id();
      }
      if (ancestor_hidden) {
        continue;
      }
      if (!layer.visible() || layer.opacity() <= 0.0) {
        continue;
      }

      const bool is_clipped = layer.clipping_mask() || ancestor_clipped;
      const Tile *src_tile = layer.image().tile_at(tx, ty);

      if (!is_clipped) {
        if (src_tile != nullptr) {
          for (std::size_t i = 0; i < comp_tile->pixels.size(); ++i) {
            base_alpha[i] = src_tile->pixels[i].a;
          }
        } else {
          std::fill(base_alpha.begin(), base_alpha.end(), 0);
        }
      }

      if (src_tile == nullptr) {
        continue;
      }

      const double opacity = layer.opacity() * parent_opacity;
      const BlendOp op = blend_op_from_string(layer.composite_op());
      const bool is_screentone = layer.screentone();
      const double st_freq = layer.screentone_frequency();
      const double st_angle = layer.screentone_angle();
      const double st_cos = std::cos(st_angle * std::numbers::pi / 180.0);
      const double st_sin = std::sin(st_angle * std::numbers::pi / 180.0);

      for (int ly = 0; ly < kTileSize; ++ly) {
        const int gy = ty * kTileSize + ly;
        for (int lx = 0; lx < kTileSize; ++lx) {
          const int gx = tx * kTileSize + lx;
          const std::size_t idx = static_cast<std::size_t>(ly) * kTileSize + lx;

          double clip = 1.0;
          if (is_clipped) {
            clip = base_alpha[idx] / 255.0;
            if (clip <= 0.001) continue;
          }

          Rgba8 src_px = src_tile->pixels[idx];
          if (is_screentone) {
            src_px = apply_screentone(src_px, gx, gy, st_freq, st_cos, st_sin,
                                      layer.screentone_pattern());
          }

          blend_layer_pixel(comp_tile->pixels[idx], src_px, opacity, op, clip);
        }
      }
    }
  }

  for (auto &layer : const_cast<std::vector<Layer> &>(layers_)) {
    layer.image().clear_dirty();
  }
  composite_dirty_ = false;
}

TiledImage &Document::composite_image() {
  update_composite();
  return composite_;
}

const TiledImage &Document::composite_image() const {
  update_composite();
  return composite_;
}

TiledImage Document::composite_with_floating() const {
  update_composite();
  TiledImage result = composite_;
  if (floating_selection_.active) {
    const bool is_pure_translation =
        (std::abs(floating_selection_.rotation) < 1e-6 &&
         std::abs(floating_selection_.scale_x - 1.0) < 1e-6 &&
         std::abs(floating_selection_.scale_y - 1.0) < 1e-6);
    if (is_pure_translation) {
      const int target_ox = floating_selection_.origin_x + floating_selection_.offset_x;
      const int target_oy = floating_selection_.origin_y + floating_selection_.offset_y;
      const int min_tx = std::max(0, target_ox / kTileSize);
      const int max_tx = std::min(result.tile_columns() - 1, (target_ox + floating_selection_.width) / kTileSize);
      const int min_ty = std::max(0, target_oy / kTileSize);
      const int max_ty = std::min(result.tile_rows() - 1, (target_oy + floating_selection_.height) / kTileSize);

      for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
          auto *comp_tile = result.writable_tile_at(tx, ty);
          if (!comp_tile) continue;

          const int tile_gx = tx * kTileSize;
          const int tile_gy = ty * kTileSize;

          const int start_x = std::max(tile_gx, target_ox);
          const int end_x = std::min(tile_gx + kTileSize, target_ox + floating_selection_.width);
          const int start_y = std::max(tile_gy, target_oy);
          const int end_y = std::min(tile_gy + kTileSize, target_oy + floating_selection_.height);

          for (int gy = start_y; gy < end_y; ++gy) {
            if (gy >= height_) continue;
            const int ly = gy - tile_gy;
            const int fs_y = gy - target_oy;
            const std::size_t fs_row = static_cast<std::size_t>(fs_y) * floating_selection_.width;
            const std::size_t comp_row = static_cast<std::size_t>(ly) * kTileSize;

            for (int gx = start_x; gx < end_x; ++gx) {
              if (gx >= width_) continue;
              const int lx = gx - tile_gx;
              const int fs_x = gx - target_ox;
              const std::size_t fs_idx = fs_row + fs_x;

              const std::uint8_t mask_val = floating_selection_.mask.empty() ? 255 : floating_selection_.mask[fs_idx];
              const auto &src = floating_selection_.pixels[fs_idx];

              if (mask_val > 0 && src.a > 0) {
                const std::size_t comp_idx = comp_row + lx;
                auto &dst = comp_tile->pixels[comp_idx];
                const std::uint8_t eff_a = (mask_val == 255) ? src.a : static_cast<std::uint8_t>((src.a * mask_val) / 255);
                if (eff_a == 255) {
                  dst = src;
                } else {
                  const std::uint32_t a = eff_a;
                  const std::uint32_t inv_a = 255 - a;
                  const std::uint32_t out_a = a + (dst.a * inv_a) / 255;
                  if (out_a > 0) {
                    dst.r = static_cast<std::uint8_t>((src.r * a + dst.r * inv_a) / 255);
                    dst.g = static_cast<std::uint8_t>((src.g * a + dst.g * inv_a) / 255);
                    dst.b = static_cast<std::uint8_t>((src.b * a + dst.b * inv_a) / 255);
                    dst.a = static_cast<std::uint8_t>(out_a);
                  }
                }
              }
            }
          }
        }
      }
    } else {
      render_transformed_floating_selection(floating_selection_, result, width_, height_);
    }
  }
  return result;
}

void Document::begin_change() {
  if (!pending_) {
    pending_ = snapshot();
  }
}

bool Document::commit_change() {
  if (!pending_) {
    return false;
  }

  DocumentSnapshot after = snapshot();
  bool changed = false;
  if (snapshots_differ(*pending_, after)) {
    if (undo_.size() >= kMaxHistory) {
      undo_.erase(undo_.begin());
    }
    undo_.push_back(HistoryEntry{std::move(*pending_), std::move(after)});
    redo_.clear();
    modified_ = true;
    changed = true;
    touch_composite();
  }
  pending_.reset();
  return changed;
}

void Document::cancel_change() {
  if (pending_) {
    restore(*pending_);
    pending_.reset();
  }
}

bool Document::undo() {
  if (floating_selection_.active) {
    cancel_move_selection();
  }
  if (undo_.empty()) {
    return false;
  }

  HistoryEntry entry = std::move(undo_.back());
  undo_.pop_back();
  restore(entry.before);
  redo_.push_back(std::move(entry));
  modified_ = true;
  return true;
}

bool Document::redo() {
  if (floating_selection_.active) {
    cancel_move_selection();
  }
  if (redo_.empty()) {
    return false;
  }

  HistoryEntry entry = std::move(redo_.back());
  redo_.pop_back();
  restore(entry.after);
  undo_.push_back(std::move(entry));
  modified_ = true;
  return true;
}

DocumentSnapshot Document::snapshot() const {
  DocumentSnapshot snap;
  snap.width = width_;
  snap.height = height_;
  snap.active_layer_index = active_layer_index_;
  snap.selection = selection_;
  snap.floating_selection = floating_selection_;
  snap.layers.reserve(layers_.size());
  for (const auto &layer : layers_) {
    LayerSnapshot lsnap;
    lsnap.name = layer.name();
    lsnap.visible = layer.visible();
    lsnap.opacity = layer.opacity();
    lsnap.composite_op = layer.composite_op();
    lsnap.locked = layer.locked();
    lsnap.image = layer.image().snapshot();
    lsnap.alpha_locked = layer.alpha_locked();
    lsnap.clipping_mask = layer.clipping_mask();
    lsnap.screentone = layer.screentone();
    lsnap.screentone_frequency = layer.screentone_frequency();
    lsnap.screentone_angle = layer.screentone_angle();
    lsnap.screentone_pattern = layer.screentone_pattern();
    lsnap.is_group = layer.is_group();
    lsnap.expanded = layer.expanded();
    lsnap.parent_id = layer.parent_id();
    lsnap.id = layer.id();
    snap.layers.push_back(std::move(lsnap));
  }
  return snap;
}

void Document::restore(const DocumentSnapshot &snapshot) {
  width_ = snapshot.width;
  height_ = snapshot.height;
  active_layer_index_ = snapshot.active_layer_index;

  std::unordered_map<std::size_t, Layer> old_layers;
  for (auto &l : layers_) {
    old_layers.emplace(l.id(), std::move(l));
  }

  layers_.clear();
  layers_.reserve(snapshot.layers.size());
  for (const auto &lsnap : snapshot.layers) {
    auto it = old_layers.find(lsnap.id);
    if (it != old_layers.end()) {
      Layer layer = std::move(it->second);
      layer.set_name(lsnap.name);
      layer.set_visible(lsnap.visible);
      layer.set_opacity(lsnap.opacity);
      layer.set_composite_op(lsnap.composite_op);
      layer.set_locked(lsnap.locked);
      layer.image().restore(lsnap.image);
      layer.set_alpha_locked(lsnap.alpha_locked);
      layer.set_clipping_mask(lsnap.clipping_mask);
      layer.set_screentone(lsnap.screentone);
      layer.set_screentone_frequency(lsnap.screentone_frequency);
      layer.set_screentone_angle(lsnap.screentone_angle);
      layer.set_screentone_pattern(lsnap.screentone_pattern);
      layer.set_is_group(lsnap.is_group);
      layer.set_expanded(lsnap.expanded);
      layer.set_parent_id(lsnap.parent_id);
      layer.set_id(lsnap.id);
      next_layer_id_ = std::max(next_layer_id_, lsnap.id);
      layers_.push_back(std::move(layer));
    } else {
      Layer layer(lsnap.name, width_, height_);
      layer.set_visible(lsnap.visible);
      layer.set_opacity(lsnap.opacity);
      layer.set_composite_op(lsnap.composite_op);
      layer.set_locked(lsnap.locked);
      layer.image().restore(lsnap.image);
      layer.set_alpha_locked(lsnap.alpha_locked);
      layer.set_clipping_mask(lsnap.clipping_mask);
      layer.set_screentone(lsnap.screentone);
      layer.set_screentone_frequency(lsnap.screentone_frequency);
      layer.set_screentone_angle(lsnap.screentone_angle);
      layer.set_screentone_pattern(lsnap.screentone_pattern);
      layer.set_is_group(lsnap.is_group);
      layer.set_expanded(lsnap.expanded);
      layer.set_parent_id(lsnap.parent_id);
      layer.set_id(lsnap.id);
      next_layer_id_ = std::max(next_layer_id_, lsnap.id);
      layers_.push_back(std::move(layer));
    }
  }
  if (active_layer_index_ >= layers_.size()) {
    active_layer_index_ = layers_.empty() ? 0 : layers_.size() - 1;
  }
  touch_composite();
  selection_ = snapshot.selection;
  floating_selection_ = snapshot.floating_selection;
}

} // namespace kreart::core
