#include "kreart/render/framebuffer.h"

#include <algorithm>

namespace kreart::render {
namespace {

std::uint32_t to_argb(core::Rgba8 color) {
  const std::uint32_t alpha = color.a;
  const std::uint32_t red = (color.r * alpha + 127U) / 255U;
  const std::uint32_t green = (color.g * alpha + 127U) / 255U;
  const std::uint32_t blue = (color.b * alpha + 127U) / 255U;
  return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

} // namespace

CanvasFramebuffer::CanvasFramebuffer(int width, int height) {
  resize(width, height);
}

void CanvasFramebuffer::resize(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  pixels_.assign(static_cast<std::size_t>(width_) * height_, 0U);
  full_repaint_ = true;
}

void CanvasFramebuffer::sync(core::TiledImage &image) {
  if (width_ != image.width() || height_ != image.height()) {
    resize(image.width(), image.height());
  }

  if (full_repaint_) {
    std::fill(pixels_.begin(), pixels_.end(), 0U);
    for (int tile_y = 0; tile_y < image.tile_rows(); ++tile_y) {
      for (int tile_x = 0; tile_x < image.tile_columns(); ++tile_x) {
        write_tile(tile_x, tile_y, image.tile_at(tile_x, tile_y));
      }
    }
  } else {
    for (const std::uint64_t key : image.dirty_tiles()) {
      const int tile_x = core::TiledImage::key_x(key);
      const int tile_y = core::TiledImage::key_y(key);
      const core::Tile *tile = image.tile_at(tile_x, tile_y);
      if (tile == nullptr) {
        clear_tile(tile_x, tile_y);
      } else {
        write_tile(tile_x, tile_y, tile);
      }
    }
  }

  image.clear_dirty();
  full_repaint_ = false;
}

void CanvasFramebuffer::write_tile(int tile_x, int tile_y, const core::Tile *tile) {
  if (tile == nullptr) {
    clear_tile(tile_x, tile_y);
    return;
  }

  const int start_x = tile_x * core::kTileSize;
  const int start_y = tile_y * core::kTileSize;
  const int end_x = std::min(start_x + core::kTileSize, width_);
  const int end_y = std::min(start_y + core::kTileSize, height_);

  for (int y = start_y; y < end_y; ++y) {
    const int local_y = y - start_y;
    for (int x = start_x; x < end_x; ++x) {
      const int local_x = x - start_x;
      const core::Rgba8 color =
          tile->pixels[static_cast<std::size_t>(local_y) * core::kTileSize + local_x];
      pixels_[static_cast<std::size_t>(y) * width_ + x] = to_argb(color);
    }
  }
}

void CanvasFramebuffer::clear_tile(int tile_x, int tile_y) {
  const int start_x = tile_x * core::kTileSize;
  const int start_y = tile_y * core::kTileSize;
  const int end_x = std::min(start_x + core::kTileSize, width_);
  const int end_y = std::min(start_y + core::kTileSize, height_);

  for (int y = start_y; y < end_y; ++y) {
    std::fill(pixels_.begin() + static_cast<std::size_t>(y) * width_ + start_x,
              pixels_.begin() + static_cast<std::size_t>(y) * width_ + end_x, 0U);
  }
}

} // namespace kreart::render
