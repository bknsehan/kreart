#include "kreart/core/tiled_image.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace kreart::core {
namespace {

constexpr Rgba8 kTransparent{0, 0, 0, 0};

std::uint8_t to_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

} // namespace

TiledImage::TiledImage(int width, int height)
    : width_(std::max(1, width)), height_(std::max(1, height)) {}

std::uint64_t TiledImage::key_for(int tile_x, int tile_y) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile_x)) << 32) |
         static_cast<std::uint32_t>(tile_y);
}

int TiledImage::key_x(std::uint64_t key) {
  return static_cast<int>(static_cast<std::uint32_t>(key >> 32));
}

int TiledImage::key_y(std::uint64_t key) {
  return static_cast<int>(static_cast<std::uint32_t>(key & 0xffffffffULL));
}

Rgba8 TiledImage::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    return kTransparent;
  }

  const int tile_x = x / kTileSize;
  const int tile_y = y / kTileSize;
  const auto found = tiles_.find(key_for(tile_x, tile_y));
  if (found == tiles_.end()) {
    return kTransparent;
  }

  const int local_x = x % kTileSize;
  const int local_y = y % kTileSize;
  return found->second->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x];
}

void TiledImage::set_pixel(int x, int y, Rgba8 color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    return;
  }

  const int tile_x = x / kTileSize;
  const int tile_y = y / kTileSize;
  const int local_x = x % kTileSize;
  const int local_y = y % kTileSize;

  if (alpha_locked_) {
    const std::uint64_t key = key_for(tile_x, tile_y);
    auto found = tiles_.find(key);
    if (found == tiles_.end()) {
      return;
    }
    const auto orig_a = found->second->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x].a;
    if (orig_a == 0) return;
    color.a = orig_a;
  }

  TilePtr &tile = writable_tile(tile_x, tile_y);
  tile->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x] = color;
  dirty_.insert(key_for(tile_x, tile_y));
  gpu_dirty_.insert(key_for(tile_x, tile_y));
}

void TiledImage::blend_pixel(int x, int y, Rgba8 source) {
  if (source.a == 0 || x < 0 || y < 0 || x >= width_ || y >= height_) {
    return;
  }

  const int tile_x = x / kTileSize;
  const int tile_y = y / kTileSize;
  const int local_x = x % kTileSize;
  const int local_y = y % kTileSize;

  if (alpha_locked_) {
    const std::uint64_t key = key_for(tile_x, tile_y);
    auto found = tiles_.find(key);
    if (found == tiles_.end()) {
      return;
    }
    const auto orig_a = found->second->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x].a;
    if (orig_a == 0) {
      return;
    }
    TilePtr &tile = writable_tile(tile_x, tile_y);
    Rgba8 &dest = tile->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x];
    const float sa = static_cast<float>(source.a) / 255.0F;
    const float inv_sa = 1.0F - sa;
    dest.r = to_byte(source.r * sa + dest.r * inv_sa);
    dest.g = to_byte(source.g * sa + dest.g * inv_sa);
    dest.b = to_byte(source.b * sa + dest.b * inv_sa);
    dest.a = orig_a;
    dirty_.insert(key);
    gpu_dirty_.insert(key);
    return;
  }

  TilePtr &tile = writable_tile(tile_x, tile_y);
  Rgba8 &destination = tile->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x];
  if (source.a == 255 || destination.a == 0) {
    if (destination.a == 0) {
      destination = source;
    } else {
      const float sa = static_cast<float>(source.a) / 255.0F;
      const float inv_sa = 1.0F - sa;
      destination.r = to_byte(source.r * sa + destination.r * inv_sa);
      destination.g = to_byte(source.g * sa + destination.g * inv_sa);
      destination.b = to_byte(source.b * sa + destination.b * inv_sa);
      destination.a = 255;
    }
  } else {
    const float sa = static_cast<float>(source.a) / 255.0F;
    const float da = static_cast<float>(destination.a) / 255.0F;
    const float out_a = sa + da * (1.0F - sa);
    if (out_a <= 0.0F) {
      destination = kTransparent;
    } else {
      destination.r = to_byte((source.r * sa + destination.r * da * (1.0F - sa)) / out_a);
      destination.g = to_byte((source.g * sa + destination.g * da * (1.0F - sa)) / out_a);
      destination.b = to_byte((source.b * sa + destination.b * da * (1.0F - sa)) / out_a);
      destination.a = to_byte(out_a * 255.0F);
    }
  }

  dirty_.insert(key_for(tile_x, tile_y));
  gpu_dirty_.insert(key_for(tile_x, tile_y));
}

void TiledImage::erase_pixel(int x, int y, std::uint8_t amount) {
  if (alpha_locked_ || amount == 0 || x < 0 || y < 0 || x >= width_ || y >= height_) {
    return;
  }

  const int tile_x = x / kTileSize;
  const int tile_y = y / kTileSize;
  const int local_x = x % kTileSize;
  const int local_y = y % kTileSize;
  const std::uint64_t key = key_for(tile_x, tile_y);
  const auto found = tiles_.find(key);
  if (found == tiles_.end()) {
    return;
  }

  TilePtr &tile = found->second;
  if (tile.use_count() > 1) {
    tile = std::make_shared<Tile>(*tile);
  }

  Rgba8 &destination = tile->pixels[static_cast<std::size_t>(local_y) * kTileSize + local_x];
  if (destination.a == 0) {
    return;
  }

  destination.a = static_cast<std::uint8_t>(
      (static_cast<unsigned int>(destination.a) * (255U - amount) + 127U) / 255U);
  if (destination.a == 0) {
    destination = kTransparent;
  }

  dirty_.insert(key);
  gpu_dirty_.insert(key);
}

void TiledImage::fill(Rgba8 color) {
  for (int tile_y = 0; tile_y < tile_rows(); ++tile_y) {
    for (int tile_x = 0; tile_x < tile_columns(); ++tile_x) {
      TilePtr &tile = writable_tile(tile_x, tile_y);
      tile->pixels.fill(color);
      dirty_.insert(key_for(tile_x, tile_y));
      gpu_dirty_.insert(key_for(tile_x, tile_y));
    }
  }
}
void TiledImage::resize(int new_width, int new_height, int offset_x, int offset_y) {
  new_width = std::max(1, new_width);
  new_height = std::max(1, new_height);
  if (width_ == new_width && height_ == new_height && offset_x == 0 && offset_y == 0) {
    return;
  }

  TiledImage new_image(new_width, new_height);

  for (const auto &[key, tile] : tiles_) {
    const int old_tx = key_x(key);
    const int old_ty = key_y(key);
    const int base_x = old_tx * kTileSize;
    const int base_y = old_ty * kTileSize;

    for (int ly = 0; ly < kTileSize; ++ly) {
      const int old_y = base_y + ly;
      if (old_y >= height_) break;
      const int target_y = old_y + offset_y;
      if (target_y < 0 || target_y >= new_height) continue;

      for (int lx = 0; lx < kTileSize; ++lx) {
        const int old_x = base_x + lx;
        if (old_x >= width_) break;
        const int target_x = old_x + offset_x;
        if (target_x < 0 || target_x >= new_width) continue;

        const Rgba8 color = tile->pixels[static_cast<std::size_t>(ly) * kTileSize + lx];
        if (color.a > 0 || color.r > 0 || color.g > 0 || color.b > 0) {
          new_image.set_pixel(target_x, target_y, color);
        }
      }
    }
  }

  width_ = new_width;
  height_ = new_height;
  tiles_ = std::move(new_image.tiles_);
  dirty_.clear();
  gpu_dirty_.clear();
  for (const auto &[key, _] : tiles_) {
    dirty_.insert(key);
    gpu_dirty_.insert(key);
  }
}
void TiledImage::scale(int new_width, int new_height, ScaleFilter filter) {
  new_width = std::max(1, new_width);
  new_height = std::max(1, new_height);
  if (width_ == new_width && height_ == new_height) {
    return;
  }
  if (tiles_.empty()) {
    width_ = new_width;
    height_ = new_height;
    return;
  }

  TiledImage new_image(new_width, new_height);
  const double scale_x = static_cast<double>(width_) / new_width;
  const double scale_y = static_cast<double>(height_) / new_height;

  auto get_pixel_clamped = [this](int px, int py) -> Rgba8 {
    px = std::clamp(px, 0, width_ - 1);
    py = std::clamp(py, 0, height_ - 1);
    return pixel(px, py);
  };

  auto sinc = [](double x) -> double {
    if (std::abs(x) < 1e-6) return 1.0;
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
  };

  auto lanczos3_weight = [sinc](double x) -> double {
    const double ax = std::abs(x);
    if (ax >= 3.0) return 0.0;
    return sinc(x) * sinc(x / 3.0);
  };

  auto cubic_weight = [](double x) -> double {
    const double ax = std::abs(x);
    if (ax <= 1.0) {
      return (1.5 * ax - 2.5) * ax * ax + 1.0;
    } else if (ax < 2.0) {
      return ((-0.5 * ax + 2.5) * ax - 4.0) * ax + 2.0;
    }
    return 0.0;
  };

  for (int y = 0; y < new_height; ++y) {
    for (int x = 0; x < new_width; ++x) {
      Rgba8 result_color = kTransparent;

      if (filter == ScaleFilter::NEAREST) {
        const int src_x = std::clamp(static_cast<int>(x * scale_x), 0, width_ - 1);
        const int src_y = std::clamp(static_cast<int>(y * scale_y), 0, height_ - 1);
        result_color = pixel(src_x, src_y);
      } else if (filter == ScaleFilter::BOX) {
        const double x0 = x * scale_x;
        const double x1 = (x + 1) * scale_x;
        const double y0 = y * scale_y;
        const double y1 = (y + 1) * scale_y;

        const int ix0 = std::clamp(static_cast<int>(std::floor(x0)), 0, width_ - 1);
        const int ix1 = std::clamp(static_cast<int>(std::ceil(x1)), 0, width_);
        const int iy0 = std::clamp(static_cast<int>(std::floor(y0)), 0, height_ - 1);
        const int iy1 = std::clamp(static_cast<int>(std::ceil(y1)), 0, height_);

        double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0, sum_a = 0.0, total_weight = 0.0;

        for (int py = iy0; py < iy1; ++py) {
          const double wy = std::max(0.0, std::min(py + 1.0, y1) - std::max(static_cast<double>(py), y0));
          if (wy <= 0.0) continue;
          for (int px = ix0; px < ix1; ++px) {
            const double wx = std::max(0.0, std::min(px + 1.0, x1) - std::max(static_cast<double>(px), x0));
            const double w = wx * wy;
            if (w <= 0.0) continue;
            const Rgba8 p = pixel(px, py);
            const double a = p.a / 255.0;
            sum_r += (p.r * a) * w;
            sum_g += (p.g * a) * w;
            sum_b += (p.b * a) * w;
            sum_a += p.a * w;
            total_weight += w;
          }
        }

        if (total_weight > 0.0 && sum_a > 0.0) {
          const double avg_a = sum_a / total_weight;
          const double norm = sum_a / 255.0;
          result_color = Rgba8{
              static_cast<std::uint8_t>(std::clamp(sum_r / norm + 0.5, 0.0, 255.0)),
              static_cast<std::uint8_t>(std::clamp(sum_g / norm + 0.5, 0.0, 255.0)),
              static_cast<std::uint8_t>(std::clamp(sum_b / norm + 0.5, 0.0, 255.0)),
              static_cast<std::uint8_t>(std::clamp(avg_a + 0.5, 0.0, 255.0))};
        }
      } else if (filter == ScaleFilter::BILINEAR) {
        const double src_xf = (x + 0.5) * scale_x - 0.5;
        const double src_yf = (y + 0.5) * scale_y - 0.5;
        const int x0 = static_cast<int>(std::floor(src_xf));
        const int y0 = static_cast<int>(std::floor(src_yf));
        const double fx = src_xf - x0;
        const double fy = src_yf - y0;

        const Rgba8 p00 = get_pixel_clamped(x0, y0);
        const Rgba8 p10 = get_pixel_clamped(x0 + 1, y0);
        const Rgba8 p01 = get_pixel_clamped(x0, y0 + 1);
        const Rgba8 p11 = get_pixel_clamped(x0 + 1, y0 + 1);

        auto interp_premul = [&](double c00, double a00, double c10, double a10,
                                 double c01, double a01, double c11, double a11) -> double {
          const double top = (c00 * a00) * (1.0 - fx) + (c10 * a10) * fx;
          const double bot = (c01 * a01) * (1.0 - fx) + (c11 * a11) * fx;
          return top * (1.0 - fy) + bot * fy;
        };

        const double a00 = p00.a / 255.0, a10 = p10.a / 255.0;
        const double a01 = p01.a / 255.0, a11 = p11.a / 255.0;
        const double top_a = p00.a * (1.0 - fx) + p10.a * fx;
        const double bot_a = p01.a * (1.0 - fx) + p11.a * fx;
        const double out_a = top_a * (1.0 - fy) + bot_a * fy;

        if (out_a > 0.0) {
          const double na = out_a / 255.0;
          const double out_r = interp_premul(p00.r, a00, p10.r, a10, p01.r, a01, p11.r, a11) / na;
          const double out_g = interp_premul(p00.g, a00, p10.g, a10, p01.g, a01, p11.g, a11) / na;
          const double out_b = interp_premul(p00.b, a00, p10.b, a10, p01.b, a01, p11.b, a11) / na;
          result_color = Rgba8{
              static_cast<std::uint8_t>(std::clamp(out_r + 0.5, 0.0, 255.0)),
              static_cast<std::uint8_t>(std::clamp(out_g + 0.5, 0.0, 255.0)),
              static_cast<std::uint8_t>(std::clamp(out_b + 0.5, 0.0, 255.0)),
              static_cast<std::uint8_t>(std::clamp(out_a + 0.5, 0.0, 255.0))};
        }
      } else {
        const int radius = (filter == ScaleFilter::LANCZOS3) ? 3 : 2;
        const double src_xf = (x + 0.5) * scale_x - 0.5;
        const double src_yf = (y + 0.5) * scale_y - 0.5;
        const int center_x = static_cast<int>(std::floor(src_xf));
        const int center_y = static_cast<int>(std::floor(src_yf));

        double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0, sum_a = 0.0, total_w = 0.0;

        for (int dy = -radius + 1; dy <= radius; ++dy) {
          const int sy = center_y + dy;
          const double wy = (filter == ScaleFilter::LANCZOS3) ? lanczos3_weight(src_yf - sy)
                                                              : cubic_weight(src_yf - sy);
          if (wy == 0.0) continue;

          for (int dx = -radius + 1; dx <= radius; ++dx) {
            const int sx = center_x + dx;
            const double wx = (filter == ScaleFilter::LANCZOS3) ? lanczos3_weight(src_xf - sx)
                                                                : cubic_weight(src_xf - sx);
            const double w = wx * wy;
            if (w == 0.0) continue;

            const Rgba8 p = get_pixel_clamped(sx, sy);
            const double a = p.a / 255.0;
            sum_r += (p.r * a) * w;
            sum_g += (p.g * a) * w;
            sum_b += (p.b * a) * w;
            sum_a += p.a * w;
            total_w += w;
          }
        }

        if (total_w > 0.0 && sum_a > 0.0) {
          const double out_a = std::clamp(sum_a / total_w, 0.0, 255.0);
          const double norm = out_a / 255.0;
          const double out_r = std::clamp((sum_r / total_w) / norm, 0.0, 255.0);
          const double out_g = std::clamp((sum_g / total_w) / norm, 0.0, 255.0);
          const double out_b = std::clamp((sum_b / total_w) / norm, 0.0, 255.0);
          result_color = Rgba8{
              static_cast<std::uint8_t>(out_r + 0.5),
              static_cast<std::uint8_t>(out_g + 0.5),
              static_cast<std::uint8_t>(out_b + 0.5),
              static_cast<std::uint8_t>(out_a + 0.5)};
        }
      }

      if (result_color.a > 0 || result_color.r > 0 || result_color.g > 0 || result_color.b > 0) {
        new_image.set_pixel(x, y, result_color);
      }
    }
  }

  width_ = new_width;
  height_ = new_height;
  tiles_ = std::move(new_image.tiles_);
  dirty_.clear();
  gpu_dirty_.clear();
  for (const auto &[key, _] : tiles_) {
    dirty_.insert(key);
    gpu_dirty_.insert(key);
  }
}



const Tile *TiledImage::tile_at(int tile_x, int tile_y) const {
  const auto found = tiles_.find(key_for(tile_x, tile_y));
  return found == tiles_.end() ? nullptr : found->second.get();
}
Tile *TiledImage::writable_tile_at(int tile_x, int tile_y) {
  dirty_.insert(key_for(tile_x, tile_y));
  gpu_dirty_.insert(key_for(tile_x, tile_y));
  return writable_tile(tile_x, tile_y).get();
}

void TiledImage::remove_tile(int tile_x, int tile_y) {
  const std::uint64_t key = key_for(tile_x, tile_y);
  auto it = tiles_.find(key);
  if (it != tiles_.end()) {
    tiles_.erase(it);
    dirty_.insert(key);
    gpu_dirty_.insert(key);
  }
}

void TiledImage::mark_tile_dirty(int tile_x, int tile_y) {
  dirty_.insert(key_for(tile_x, tile_y));
  gpu_dirty_.insert(key_for(tile_x, tile_y));
}


ImageSnapshot TiledImage::snapshot() const {
  return ImageSnapshot{width_, height_, tiles_};
}

void TiledImage::restore(const ImageSnapshot &snapshot) {
  width_ = snapshot.width;
  height_ = snapshot.height;
  std::unordered_set<std::uint64_t> all_keys = std::move(dirty_);
  for (std::uint64_t key : gpu_dirty_) {
    all_keys.insert(key);
  }
  for (const auto &[key, _] : tiles_) {
    all_keys.insert(key);
  }
  for (const auto &[key, _] : snapshot.tiles) {
    all_keys.insert(key);
  }
  tiles_ = snapshot.tiles;
  dirty_ = all_keys;
  gpu_dirty_ = std::move(all_keys);
}

TilePtr &TiledImage::writable_tile(int tile_x, int tile_y) {
  const std::uint64_t key = key_for(tile_x, tile_y);
  dirty_.insert(key);
  gpu_dirty_.insert(key);
  TilePtr &tile = tiles_[key];
  if (!tile) {
    tile = std::make_shared<Tile>();
  } else if (tile.use_count() > 1) {
    tile = std::make_shared<Tile>(*tile);
  }
  return tile;
}

std::optional<SelectionRect> TiledImage::content_bounds() const {
  if (tiles_.empty()) {
    return std::nullopt;
  }

  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
  bool found = false;

  constexpr std::size_t kWordCount = (kTileSize * kTileSize * sizeof(Rgba8)) / sizeof(std::uint64_t);

  for (const auto &[key, tile] : tiles_) {
    if (!tile) {
      continue;
    }

    const auto *words = reinterpret_cast<const std::uint64_t *>(tile->pixels.data());
    bool tile_has_content = false;
    for (std::size_t i = 0; i < kWordCount; ++i) {
      if (words[i] != 0) {
        tile_has_content = true;
        break;
      }
    }
    if (!tile_has_content) {
      continue;
    }

    const int tx = key_x(key);
    const int ty = key_y(key);
    const int base_x = tx * kTileSize;
    const int base_y = ty * kTileSize;

    for (int ly = 0; ly < kTileSize; ++ly) {
      const int py = base_y + ly;
      if (py < 0 || py >= height_) {
        continue;
      }
      const std::size_t row_offset = static_cast<std::size_t>(ly) * kTileSize;
      for (int lx = 0; lx < kTileSize; ++lx) {
        const int px = base_x + lx;
        if (px < 0 || px >= width_) {
          continue;
        }
        if (tile->pixels[row_offset + static_cast<std::size_t>(lx)].a > 0) {
          min_x = std::min(min_x, px);
          min_y = std::min(min_y, py);
          max_x = std::max(max_x, px);
          max_y = std::max(max_y, py);
          found = true;
        }
      }
    }
  }

  if (!found) {
    return std::nullopt;
  }

  return SelectionRect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

} // namespace kreart::core
