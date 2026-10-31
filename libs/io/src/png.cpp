#include "kreart/io/png.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <png.h>

namespace kreart::io {

bool write_png(const std::filesystem::path &path, const core::TiledImage &image,
               std::string &error) {
  png_image png{};
  png.version = PNG_IMAGE_VERSION;
  png.width = static_cast<png_uint_32>(image.width());
  png.height = static_cast<png_uint_32>(image.height());
  png.format = PNG_FORMAT_RGBA;

  // Tile-oriented copy: image.pixel(x, y) is a hash lookup per pixel, so the
  // full-document export used to be quadratic in tile-map overhead.
  std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(png));
  std::fill(pixels.begin(), pixels.end(), 0);
  const int w = image.width();
  const int h = image.height();
  for (const auto &[key, tile] : image.tiles()) {
    const int tx = core::TiledImage::key_x(key);
    const int ty = core::TiledImage::key_y(key);
    const int x0 = tx * core::kTileSize;
    const int y0 = ty * core::kTileSize;
    const int nx = std::min(core::kTileSize, w - x0);
    const int ny = std::min(core::kTileSize, h - y0);
    if (nx <= 0 || ny <= 0 || tile == nullptr) continue;
    for (int ly = 0; ly < ny; ++ly) {
      std::uint8_t *dst = pixels.data() + (static_cast<std::size_t>(y0 + ly) * w + x0) * 4;
      const core::Rgba8 *src = tile->pixels.data() + static_cast<std::size_t>(ly) * core::kTileSize;
      for (int lx = 0; lx < nx; ++lx) {
        dst[lx * 4 + 0] = src[lx].r;
        dst[lx * 4 + 1] = src[lx].g;
        dst[lx * 4 + 2] = src[lx].b;
        dst[lx * 4 + 3] = src[lx].a;
      }
    }
  }

  const std::string native_path = path.string();
  if (png_image_write_to_file(&png, native_path.c_str(), 0, pixels.data(), 0, nullptr) == 0) {
    error = png.message[0] != '\0' ? png.message : "Unknown libpng write error";
    png_image_free(&png);
    return false;
  }

  png_image_free(&png);
  error.clear();
  return true;
}

} // namespace kreart::io
