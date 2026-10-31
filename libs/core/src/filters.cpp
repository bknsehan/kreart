#include "kreart/core/filters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace kreart::core {

namespace {

inline void rgb_to_hsv(double r, double g, double b, double &h, double &s, double &v) {
  const double max_val = std::max({r, g, b});
  const double min_val = std::min({r, g, b});
  const double delta = max_val - min_val;

  v = max_val;
  s = (max_val > 1e-6) ? (delta / max_val) : 0.0;

  if (delta < 1e-6) {
    h = 0.0;
    return;
  }

  if (std::abs(max_val - r) < 1e-6) {
    h = 60.0 * std::fmod((g - b) / delta, 6.0);
  } else if (std::abs(max_val - g) < 1e-6) {
    h = 60.0 * (((b - r) / delta) + 2.0);
  } else {
    h = 60.0 * (((r - g) / delta) + 4.0);
  }

  if (h < 0.0) {
    h += 360.0;
  }
}

inline void hsv_to_rgb(double h, double s, double v, double &r, double &g, double &b) {
  if (s <= 1e-6) {
    r = v;
    g = v;
    b = v;
    return;
  }

  const double c = v * s;
  const double hh = std::fmod(h, 360.0) / 60.0;
  const double x = c * (1.0 - std::abs(std::fmod(hh, 2.0) - 1.0));
  const double m = v - c;

  if (hh >= 0.0 && hh < 1.0) {
    r = c; g = x; b = 0.0;
  } else if (hh >= 1.0 && hh < 2.0) {
    r = x; g = c; b = 0.0;
  } else if (hh >= 2.0 && hh < 3.0) {
    r = 0.0; g = c; b = x;
  } else if (hh >= 3.0 && hh < 4.0) {
    r = 0.0; g = x; b = c;
  } else if (hh >= 4.0 && hh < 5.0) {
    r = x; g = 0.0; b = c;
  } else {
    r = c; g = 0.0; b = x;
  }

  r += m;
  g += m;
  b += m;
}

inline Rgba8 blend_selection(Rgba8 orig, Rgba8 modified, std::uint8_t sel) {
  if (sel == 255) return modified;
  if (sel == 0) return orig;
  const double w = sel / 255.0;
  return Rgba8{
      static_cast<std::uint8_t>(std::clamp(std::lround(orig.r * (1.0 - w) + modified.r * w), 0L, 255L)),
      static_cast<std::uint8_t>(std::clamp(std::lround(orig.g * (1.0 - w) + modified.g * w), 0L, 255L)),
      static_cast<std::uint8_t>(std::clamp(std::lround(orig.b * (1.0 - w) + modified.b * w), 0L, 255L)),
      static_cast<std::uint8_t>(std::clamp(std::lround(orig.a * (1.0 - w) + modified.a * w), 0L, 255L))
  };
}

} // namespace

void filter_hsv(TiledImage &image, double hue_shift_deg, double saturation_scale,
                double value_scale, const SelectionMask *selection) {
  const bool has_selection = selection && selection->active();

  for (int ty = 0; ty < image.tile_rows(); ++ty) {
    for (int tx = 0; tx < image.tile_columns(); ++tx) {
      if (image.tile_at(tx, ty) == nullptr) continue;

      Tile *tile = image.writable_tile_at(tx, ty);
      bool tile_changed = false;

      for (int ly = 0; ly < kTileSize; ++ly) {
        const int gy = ty * kTileSize + ly;
        if (gy >= image.height()) break;

        for (int lx = 0; lx < kTileSize; ++lx) {
          const int gx = tx * kTileSize + lx;
          if (gx >= image.width()) break;

          const std::size_t idx = static_cast<std::size_t>(ly) * kTileSize + lx;
          const Rgba8 orig = tile->pixels[idx];
          if (orig.a == 0) continue;

          std::uint8_t sel_val = 255;
          if (has_selection) {
            sel_val = selection->value(gx, gy);
            if (sel_val == 0) continue;
          }

          double h = 0.0, s = 0.0, v = 0.0;
          rgb_to_hsv(orig.r / 255.0, orig.g / 255.0, orig.b / 255.0, h, s, v);

          h = std::fmod(h + hue_shift_deg + 360.0, 360.0);
          if (saturation_scale < 0.0) {
            s *= (1.0 + saturation_scale);
          } else {
            s = s + (1.0 - s) * saturation_scale;
          }
          s = std::clamp(s, 0.0, 1.0);

          if (value_scale < 0.0) {
            v *= (1.0 + value_scale);
          } else {
            v = v + (1.0 - v) * value_scale;
          }
          v = std::clamp(v, 0.0, 1.0);

          double nr = 0.0, ng = 0.0, nb = 0.0;
          hsv_to_rgb(h, s, v, nr, ng, nb);

          const Rgba8 modified{
              static_cast<std::uint8_t>(std::clamp(std::lround(nr * 255.0), 0L, 255L)),
              static_cast<std::uint8_t>(std::clamp(std::lround(ng * 255.0), 0L, 255L)),
              static_cast<std::uint8_t>(std::clamp(std::lround(nb * 255.0), 0L, 255L)),
              orig.a
          };

          tile->pixels[idx] = has_selection ? blend_selection(orig, modified, sel_val) : modified;
          tile_changed = true;
        }
      }

      if (tile_changed) {
        image.mark_tile_dirty(tx, ty);
      }
    }
  }
}

void filter_brightness_contrast(TiledImage &image, double brightness, double contrast,
                                const SelectionMask *selection) {
  const bool has_selection = selection && selection->active();

  // Precompute 256-entry lookup table for O(1) pixel transformation
  std::array<std::uint8_t, 256> lut;
  const double clamped_contrast = std::clamp(contrast, -100.0, 100.0);
  const double factor = (259.0 * (clamped_contrast + 255.0)) / (255.0 * (259.0 - clamped_contrast));
  const double b_shift = brightness * 2.55;

  for (int i = 0; i < 256; ++i) {
    const double val = factor * ((static_cast<double>(i) + b_shift) - 128.0) + 128.0;
    lut[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::clamp(std::lround(val), 0L, 255L));
  }

  for (int ty = 0; ty < image.tile_rows(); ++ty) {
    for (int tx = 0; tx < image.tile_columns(); ++tx) {
      if (image.tile_at(tx, ty) == nullptr) continue;

      Tile *tile = image.writable_tile_at(tx, ty);
      bool tile_changed = false;

      for (int ly = 0; ly < kTileSize; ++ly) {
        const int gy = ty * kTileSize + ly;
        if (gy >= image.height()) break;

        for (int lx = 0; lx < kTileSize; ++lx) {
          const int gx = tx * kTileSize + lx;
          if (gx >= image.width()) break;

          const std::size_t idx = static_cast<std::size_t>(ly) * kTileSize + lx;
          const Rgba8 orig = tile->pixels[idx];
          if (orig.a == 0) continue;

          std::uint8_t sel_val = 255;
          if (has_selection) {
            sel_val = selection->value(gx, gy);
            if (sel_val == 0) continue;
          }

          const Rgba8 modified{
              lut[orig.r],
              lut[orig.g],
              lut[orig.b],
              orig.a
          };

          tile->pixels[idx] = has_selection ? blend_selection(orig, modified, sel_val) : modified;
          tile_changed = true;
        }
      }

      if (tile_changed) {
        image.mark_tile_dirty(tx, ty);
      }
    }
  }
}

void filter_color_balance(TiledImage &image, const ColorBalanceParams &params,
                          const SelectionMask *selection) {
  const bool has_selection = selection && selection->active();

  for (int ty = 0; ty < image.tile_rows(); ++ty) {
    for (int tx = 0; tx < image.tile_columns(); ++tx) {
      if (image.tile_at(tx, ty) == nullptr) continue;

      Tile *tile = image.writable_tile_at(tx, ty);
      bool tile_changed = false;

      for (int ly = 0; ly < kTileSize; ++ly) {
        const int gy = ty * kTileSize + ly;
        if (gy >= image.height()) break;

        for (int lx = 0; lx < kTileSize; ++lx) {
          const int gx = tx * kTileSize + lx;
          if (gx >= image.width()) break;

          const std::size_t idx = static_cast<std::size_t>(ly) * kTileSize + lx;
          const Rgba8 orig = tile->pixels[idx];
          if (orig.a == 0) continue;

          std::uint8_t sel_val = 255;
          if (has_selection) {
            sel_val = selection->value(gx, gy);
            if (sel_val == 0) continue;
          }

          const double l = (0.299 * orig.r + 0.587 * orig.g + 0.114 * orig.b) / 255.0;

          // Shadows peak at l=0, Midtones peak at l=0.5, Highlights peak at l=1.0
          const double w_shadows = std::clamp(1.0 - 2.0 * l, 0.0, 1.0);
          const double w_highlights = std::clamp(2.0 * l - 1.0, 0.0, 1.0);
          const double w_midtones = std::clamp(1.0 - 2.0 * std::abs(l - 0.5), 0.0, 1.0);

          const double dr = (params.shadows.cyan_red * w_shadows +
                             params.midtones.cyan_red * w_midtones +
                             params.highlights.cyan_red * w_highlights) * 1.28;

          const double dg = (params.shadows.magenta_green * w_shadows +
                             params.midtones.magenta_green * w_midtones +
                             params.highlights.magenta_green * w_highlights) * 1.28;

          const double db = (params.shadows.yellow_blue * w_shadows +
                             params.midtones.yellow_blue * w_midtones +
                             params.highlights.yellow_blue * w_highlights) * 1.28;

          double nr = std::clamp(orig.r + dr, 0.0, 255.0);
          double ng = std::clamp(orig.g + dg, 0.0, 255.0);
          double nb = std::clamp(orig.b + db, 0.0, 255.0);

          if (params.preserve_luminosity) {
            const double new_l = 0.299 * nr + 0.587 * ng + 0.114 * nb;
            const double orig_lum = l * 255.0;
            if (new_l > 1e-4) {
              const double scale = orig_lum / new_l;
              nr = std::clamp(nr * scale, 0.0, 255.0);
              ng = std::clamp(ng * scale, 0.0, 255.0);
              nb = std::clamp(nb * scale, 0.0, 255.0);
            }
          }

          const Rgba8 modified{
              static_cast<std::uint8_t>(std::lround(nr)),
              static_cast<std::uint8_t>(std::lround(ng)),
              static_cast<std::uint8_t>(std::lround(nb)),
              orig.a
          };

          tile->pixels[idx] = has_selection ? blend_selection(orig, modified, sel_val) : modified;
          tile_changed = true;
        }
      }

      if (tile_changed) {
        image.mark_tile_dirty(tx, ty);
      }
    }
  }
}

void filter_gaussian_blur(TiledImage &image, double radius,
                          const SelectionMask *selection) {
  if (radius <= 0.1 || image.width() <= 0 || image.height() <= 0) {
    return;
  }

  const int w = image.width();
  const int h = image.height();
  const bool has_selection = selection && selection->active();

  // Determine active blur bounds
  int min_x = 0, max_x = w - 1, min_y = 0, max_y = h - 1;
  if (has_selection) {
    const auto &sb = selection->bounds();
    min_x = std::max(0, sb.x);
    max_x = std::min(w - 1, sb.x + sb.width - 1);
    min_y = std::max(0, sb.y);
    max_y = std::min(h - 1, sb.y + sb.height - 1);
    if (min_x > max_x || min_y > max_y) return;
  }

  // 1D Gaussian kernel
  const double sigma = std::max(0.5, radius * 0.5);
  const int k = std::min(std::max(1, static_cast<int>(std::ceil(3.0 * sigma))), 50);
  std::vector<double> kernel(2 * k + 1);
  double sum = 0.0;
  for (int i = -k; i <= k; ++i) {
    const double val = std::exp(-(i * i) / (2.0 * sigma * sigma));
    kernel[i + k] = val;
    sum += val;
  }
  for (double &val : kernel) {
    val /= sum;
  }

  // Extract source buffer with padding for separable convolution
  const int pad_min_x = std::max(0, min_x - k);
  const int pad_max_x = std::min(w - 1, max_x + k);
  const int pad_min_y = std::max(0, min_y - k);
  const int pad_max_y = std::min(h - 1, max_y + k);

  const int rw = pad_max_x - pad_min_x + 1;
  const int rh = pad_max_y - pad_min_y + 1;

  struct PremulPixel {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 0.0F;
  };

  std::vector<PremulPixel> src(static_cast<std::size_t>(rw) * rh);
  for (int y = 0; y < rh; ++y) {
    const int gy = pad_min_y + y;
    for (int x = 0; x < rw; ++x) {
      const int gx = pad_min_x + x;
      const Rgba8 px = image.pixel(gx, gy);
      const float a = px.a / 255.0F;
      src[y * rw + x] = PremulPixel{px.r * a, px.g * a, px.b * a, a};
    }
  }

  // Pass 1: Horizontal 1D convolution
  std::vector<PremulPixel> temp(static_cast<std::size_t>(rw) * rh);
  for (int y = 0; y < rh; ++y) {
    for (int x = 0; x < rw; ++x) {
      float tr = 0.0F, tg = 0.0F, tb = 0.0F, ta = 0.0F;
      for (int i = -k; i <= k; ++i) {
        const int sx = std::clamp(x + i, 0, rw - 1);
        const float kw = static_cast<float>(kernel[i + k]);
        const auto &p = src[y * rw + sx];
        tr += p.r * kw;
        tg += p.g * kw;
        tb += p.b * kw;
        ta += p.a * kw;
      }
      temp[y * rw + x] = PremulPixel{tr, tg, tb, ta};
    }
  }

  // Pass 2: Vertical 1D convolution & writeback to TiledImage
  int last_tx = -1, last_ty = -1;
  Tile *cached_tile = nullptr;

  for (int gy = min_y; gy <= max_y; ++gy) {
    const int y = gy - pad_min_y;
    const int ty = gy / kTileSize;
    const int ly = gy % kTileSize;

    for (int gx = min_x; gx <= max_x; ++gx) {
      const int x = gx - pad_min_x;
      const int tx = gx / kTileSize;
      const int lx = gx % kTileSize;

      float tr = 0.0F, tg = 0.0F, tb = 0.0F, ta = 0.0F;
      for (int i = -k; i <= k; ++i) {
        const int sy = std::clamp(y + i, 0, rh - 1);
        const float kw = static_cast<float>(kernel[i + k]);
        const auto &p = temp[sy * rw + x];
        tr += p.r * kw;
        tg += p.g * kw;
        tb += p.b * kw;
        ta += p.a * kw;
      }

      Rgba8 blurred;
      if (ta > 1e-4F) {
        blurred.r = static_cast<std::uint8_t>(std::clamp(std::lround(tr / ta), 0L, 255L));
        blurred.g = static_cast<std::uint8_t>(std::clamp(std::lround(tg / ta), 0L, 255L));
        blurred.b = static_cast<std::uint8_t>(std::clamp(std::lround(tb / ta), 0L, 255L));
        blurred.a = static_cast<std::uint8_t>(std::clamp(std::lround(ta * 255.0F), 0L, 255L));
      } else {
        blurred = Rgba8{0, 0, 0, 0};
      }

      if (tx != last_tx || ty != last_ty) {
        last_tx = tx;
        last_ty = ty;
        cached_tile = image.writable_tile_at(tx, ty);
        image.mark_tile_dirty(tx, ty);
      }

      if (cached_tile != nullptr) {
        const std::size_t idx = static_cast<std::size_t>(ly) * kTileSize + lx;
        const Rgba8 orig = cached_tile->pixels[idx];
        if (has_selection) {
          const std::uint8_t sel_val = selection->value(gx, gy);
          cached_tile->pixels[idx] = blend_selection(orig, blurred, sel_val);
        } else {
          cached_tile->pixels[idx] = blurred;
        }
      }
    }
  }
}

} // namespace kreart::core
