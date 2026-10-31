#pragma once

#include <cstdint>
#include <vector>

#include "kreart/core/tiled_image.h"

namespace kreart::render {

// CPU display buffer in Cairo ARGB32 byte order (premultiplied alpha on
// little-endian systems). This is the reference path until the OpenGL renderer
// is introduced.
class CanvasFramebuffer {
public:
  CanvasFramebuffer(int width, int height);

  int width() const {
    return width_;
  }
  int height() const {
    return height_;
  }
  int stride() const {
    return width_ * 4;
  }

  std::uint32_t *data() {
    return pixels_.data();
  }
  const std::uint32_t *data() const {
    return pixels_.data();
  }

  void sync(core::TiledImage &image);
  void mark_all_dirty() {
    full_repaint_ = true;
  }

private:
  void resize(int width, int height);
  void write_tile(int tile_x, int tile_y, const core::Tile *tile);
  void clear_tile(int tile_x, int tile_y);

  int width_ = 0;
  int height_ = 0;
  bool full_repaint_ = true;
  std::vector<std::uint32_t> pixels_;
};

} // namespace kreart::render
