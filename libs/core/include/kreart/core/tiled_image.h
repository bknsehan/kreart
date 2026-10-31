#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "kreart/core/selection_mask.h"
namespace kreart::core {

inline constexpr int kTileSize = 128;

struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;
  bool operator==(const Rgba8 &) const = default;
};

struct Tile {
  std::array<Rgba8, kTileSize * kTileSize> pixels{};
};

using TilePtr = std::shared_ptr<Tile>;
enum class ScaleFilter {
  NEAREST,
  BOX,
  BILINEAR,
  BICUBIC,
  LANCZOS3,
};


struct ImageSnapshot {
  int width = 0;
  int height = 0;
  std::unordered_map<std::uint64_t, TilePtr> tiles;
};

class TiledImage {
public:
  TiledImage(int width, int height);

  int width() const {
    return width_;
  }
  int height() const {
    return height_;
  }
  int tile_columns() const {
    return (width_ + kTileSize - 1) / kTileSize;
  }
  int tile_rows() const {
    return (height_ + kTileSize - 1) / kTileSize;
  }

  Rgba8 pixel(int x, int y) const;
  void set_pixel(int x, int y, Rgba8 color);
  void blend_pixel(int x, int y, Rgba8 color);
  void erase_pixel(int x, int y, std::uint8_t amount);

  bool alpha_locked() const {
    return alpha_locked_;
  }
  void set_alpha_locked(bool locked) {
    alpha_locked_ = locked;
  }

  void fill(Rgba8 color);
  void resize(int new_width, int new_height, int offset_x = 0, int offset_y = 0);
  void scale(int new_width, int new_height, ScaleFilter filter);

  std::optional<SelectionRect> content_bounds() const;


  const Tile *tile_at(int tile_x, int tile_y) const;
  Tile *writable_tile_at(int tile_x, int tile_y);
  void remove_tile(int tile_x, int tile_y);
  void mark_tile_dirty(int tile_x, int tile_y);
  const std::unordered_map<std::uint64_t, TilePtr> &tiles() const {
    return tiles_;
  }
  const std::unordered_set<std::uint64_t> &dirty_tiles() const {
    return dirty_;
  }
  void clear_dirty() {
    dirty_.clear();
  }
  // Separate dirty set for GPU consumers. The CPU composite path clears
  // dirty_ but must not hide tiles the GL renderer has not uploaded yet.
  const std::unordered_set<std::uint64_t> &gpu_dirty_tiles() const {
    return gpu_dirty_;
  }
  void clear_gpu_dirty() {
    gpu_dirty_.clear();
  }

  ImageSnapshot snapshot() const;
  void restore(const ImageSnapshot &snapshot);

  static std::uint64_t key_for(int tile_x, int tile_y);
  static int key_x(std::uint64_t key);
  static int key_y(std::uint64_t key);

private:
  TilePtr &writable_tile(int tile_x, int tile_y);

  int width_ = 0;
  int height_ = 0;
  std::unordered_map<std::uint64_t, TilePtr> tiles_;
  std::unordered_set<std::uint64_t> dirty_;
  std::unordered_set<std::uint64_t> gpu_dirty_;
  bool alpha_locked_ = false;
};

} // namespace kreart::core
