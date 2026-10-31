#include "kreart/brush/texture_mask.h"
#include "kreart/brush/brush_tip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace kreart::brush {

TextureMask::TextureMask(int width, int height, std::vector<std::uint8_t> data, std::string name)
    : width_(width), height_(height), data_(std::move(data)), name_(std::move(name)) {}

double TextureMask::sample(double doc_x, double doc_y, double scale,
                           double offset_x, double offset_y) const {
  if (empty()) {
    return 1.0;
  }
  const double sc = (scale <= 0.01) ? 0.01 : scale;
  const double u = (doc_x / sc) + offset_x;
  const double v = (doc_y / sc) + offset_y;

  double fx = std::fmod(u, static_cast<double>(width_));
  if (fx < 0.0) fx += width_;
  double fy = std::fmod(v, static_cast<double>(height_));
  if (fy < 0.0) fy += height_;

  const int x0 = static_cast<int>(std::floor(fx)) % width_;
  const int y0 = static_cast<int>(std::floor(fy)) % height_;
  const int x1 = (x0 + 1) % width_;
  const int y1 = (y0 + 1) % height_;

  const double wx = fx - std::floor(fx);
  const double wy = fy - std::floor(fy);

  const double v00 = data_[y0 * width_ + x0] / 255.0;
  const double v10 = data_[y0 * width_ + x1] / 255.0;
  const double v01 = data_[y1 * width_ + x0] / 255.0;
  const double v11 = data_[y1 * width_ + x1] / 255.0;

  return (1.0 - wx) * ((1.0 - wy) * v00 + wy * v01) +
         wx * ((1.0 - wy) * v10 + wy * v11);
}

std::shared_ptr<TextureMask> TextureMask::load_from_memory(const std::uint8_t *data,
                                                           std::size_t size, std::string name) {
  if (!data || size < 8) return nullptr;

  if (auto pat = load_pat(data, size, name)) {
    return pat;
  }
  return load_png(data, size, name);
}

std::shared_ptr<TextureMask> TextureMask::load_pat(const std::uint8_t *data,
                                                  std::size_t size, std::string name) {
  if (!data || size < 24) return nullptr;

  auto read_u32 = [](const std::uint8_t *p) -> std::uint32_t {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           (static_cast<std::uint32_t>(p[3]));
  };

  const std::uint32_t hdr_size = read_u32(data);
  const std::uint32_t version = read_u32(data + 4);
  const std::uint32_t width = read_u32(data + 8);
  const std::uint32_t height = read_u32(data + 12);
  const std::uint32_t bpp = read_u32(data + 16);

  if (version != 1 || width == 0 || height == 0 || width > 4096 || height > 4096) {
    return nullptr;
  }
  if (bpp != 1 && bpp != 3 && bpp != 4) {
    return nullptr;
  }
  if (hdr_size < 20 || hdr_size >= size) {
    return nullptr;
  }

  const std::size_t expected_data_size = static_cast<std::size_t>(width) * height * bpp;
  if (size < hdr_size + expected_data_size) {
    return nullptr;
  }

  std::string pat_name = name;
  if (pat_name.empty() && hdr_size > 20) {
    pat_name = reinterpret_cast<const char *>(data + 20);
  }

  const std::uint8_t *src_pixels = data + hdr_size;
  std::vector<std::uint8_t> out_data(static_cast<std::size_t>(width) * height);

  if (bpp == 1) {
    std::memcpy(out_data.data(), src_pixels, out_data.size());
  } else if (bpp == 3) {
    for (std::size_t i = 0; i < out_data.size(); ++i) {
      const double r = src_pixels[i * 3 + 0];
      const double g = src_pixels[i * 3 + 1];
      const double b = src_pixels[i * 3 + 2];
      out_data[i] = static_cast<std::uint8_t>(std::clamp(
          std::lround(0.299 * r + 0.587 * g + 0.114 * b), 0L, 255L));
    }
  } else if (bpp == 4) {
    for (std::size_t i = 0; i < out_data.size(); ++i) {
      const double r = src_pixels[i * 4 + 0];
      const double g = src_pixels[i * 4 + 1];
      const double b = src_pixels[i * 4 + 2];
      const double a = src_pixels[i * 4 + 3] / 255.0;
      const double lum = 0.299 * r + 0.587 * g + 0.114 * b;
      out_data[i] = static_cast<std::uint8_t>(std::clamp(std::lround(lum * a), 0L, 255L));
    }
  }

  return std::make_shared<TextureMask>(static_cast<int>(width), static_cast<int>(height),
                                       std::move(out_data), std::move(pat_name));
}

std::shared_ptr<TextureMask> TextureMask::load_png(const std::uint8_t *data,
                                                  std::size_t size, std::string name) {
  auto tip = BrushTip::load_png(data, size);
  if (!tip || tip->empty()) {
    return nullptr;
  }
  return std::make_shared<TextureMask>(tip->width(), tip->height(), tip->mask(), std::move(name));
}

std::shared_ptr<TextureMask> TextureMask::create_rough_paper(int size) {
  size = std::clamp(size, 32, 512);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * size);

  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double f1 = std::sin(x * 0.8) * std::cos(y * 0.8);
      const double f2 = std::sin(x * 1.5 + y * 0.4) * std::cos(y * 1.5 - x * 0.4);
      const double m1 = std::sin(x * 0.25) * std::sin(y * 0.25);
      const double noise = (((x * 1597 + y * 51749 + (x ^ y) * 38249) % 256) / 255.0);

      double val = 0.5 + 0.2 * f1 + 0.15 * f2 + 0.1 * m1 + 0.2 * (noise - 0.5);
      val = std::clamp(val, 0.0, 1.0);
      data[static_cast<std::size_t>(y) * size + x] =
          static_cast<std::uint8_t>(std::lround(val * 255.0));
    }
  }

  return std::make_shared<TextureMask>(size, size, std::move(data), "Rough Paper");
}

std::shared_ptr<TextureMask> TextureMask::create_canvas_grain(int size) {
  size = std::clamp(size, 32, 512);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * size);

  constexpr double kPeriod = 8.0;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double warp = std::sin(x * (2.0 * std::numbers::pi / kPeriod));
      const double weft = std::cos(y * (2.0 * std::numbers::pi / kPeriod));
      const double crossover = (warp - weft) * 0.5;
      const double noise = (((x * 1337 + y * 7331) % 256) / 255.0) * 0.15;

      double val = 0.5 + 0.35 * crossover + noise;
      val = std::clamp(val, 0.0, 1.0);
      data[static_cast<std::size_t>(y) * size + x] =
          static_cast<std::uint8_t>(std::lround(val * 255.0));
    }
  }

  return std::make_shared<TextureMask>(size, size, std::move(data), "Canvas Grain");
}

std::shared_ptr<TextureMask> TextureMask::create_charcoal_tooth(int size) {
  size = std::clamp(size, 32, 512);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * size);

  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double p1 = std::sin(x * 0.5) * std::cos(y * 0.5);
      const double n1 = (((x * 7919 + y * 65537 + (x * y) * 31) % 256) / 255.0);
      const double n2 = ((((x + 3) * 31337 + (y + 7) * 48271) % 256) / 255.0);

      double val = 0.3 + 0.25 * p1 + 0.35 * n1 + 0.15 * n2;
      val = std::clamp(val, 0.0, 1.0);
      data[static_cast<std::size_t>(y) * size + x] =
          static_cast<std::uint8_t>(std::lround(val * 255.0));
    }
  }

  return std::make_shared<TextureMask>(size, size, std::move(data), "Charcoal Tooth");
}

} // namespace kreart::brush
