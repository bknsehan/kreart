#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include <algorithm>

namespace kreart::brush {

class BrushTip {
public:
  BrushTip() = default;
  BrushTip(int width, int height, std::vector<std::uint8_t> mask);

  int width() const {
    return width_;
  }
  int height() const {
    return height_;
  }
  bool empty() const {
    return mask_.empty() || width_ <= 0 || height_ <= 0;
  }

  // Returns coverage in [0.0, 1.0] at normalized coordinates u, v in [0.0, 1.0].
  inline double sample(double u, double v) const {
    if (mask_.empty() || width_ <= 0 || height_ <= 0) {
      return 1.0;
    }
    const double clamped_u = std::clamp(u, 0.0, 1.0);
    const double clamped_v = std::clamp(v, 0.0, 1.0);
    const double fx = clamped_u * (width_ - 1);
    const double fy = clamped_v * (height_ - 1);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const double wx = fx - x0;
    const double wy = fy - y0;
    const std::size_t row0 = static_cast<std::size_t>(y0) * width_;
    const std::size_t row1 = static_cast<std::size_t>(y1) * width_;
    const double v00 = mask_[row0 + x0];
    const double v10 = mask_[row0 + x1];
    const double v01 = mask_[row1 + x0];
    const double v11 = mask_[row1 + x1];
    const double top = v00 * (1.0 - wx) + v10 * wx;
    const double bot = v01 * (1.0 - wx) + v11 * wx;
    return (top * (1.0 - wy) + bot * wy) * (1.0 / 255.0);
  }

  const std::vector<std::uint8_t> &mask() const {
    return mask_;
  }

  static std::shared_ptr<BrushTip> load_from_memory(const std::uint8_t *data, std::size_t size,
                                                    std::string_view filename);
  static std::shared_ptr<BrushTip> load_gbr(const std::uint8_t *data, std::size_t size);
  static std::shared_ptr<BrushTip> load_gih(const std::uint8_t *data, std::size_t size);
  static std::shared_ptr<BrushTip> load_png(const std::uint8_t *data, std::size_t size);

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> mask_;
};

using BrushTipPtr = std::shared_ptr<BrushTip>;

} // namespace kreart::brush
