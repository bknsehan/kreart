#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace kreart::core {
class TiledImage;


struct SelectionRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool operator==(const SelectionRect &) const = default;
};

enum class SelectionCombineMode {
  REPLACE,
  ADD,
  SUBTRACT,
  INTERSECT,
};

class SelectionMask {
public:
  SelectionMask() = default;
  SelectionMask(int width, int height);

  void reset(int width, int height);

  int width() const {
    return width_;
  }
  int height() const {
    return height_;
  }

  bool active() const {
    return active_;
  }
  bool has_selection() const {
    return active_ && bounds_.width > 0 && bounds_.height > 0;
  }

  bool operator==(const SelectionMask &other) const {
    if (active_ != other.active_) return false;
    if (!active_) return true;
    return width_ == other.width_ && height_ == other.height_ &&
           bounds_ == other.bounds_ && mask_ == other.mask_ &&
           lasso_points_ == other.lasso_points_;
  }


  std::uint8_t value(int x, int y) const {
    if (!active_) {
      return 255;
    }
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || mask_.empty()) {
      return 0;
    }
    return mask_[static_cast<std::size_t>(y) * width_ + x];
  }

  bool is_selected(int x, int y) const {
    return value(x, y) > 0;
  }

  void apply_combine_mask(const std::vector<std::uint8_t> &incoming,
                          const SelectionRect &incoming_bounds,
                          SelectionCombineMode mode);
  void set_rect(int x, int y, int w, int h, SelectionCombineMode mode = SelectionCombineMode::REPLACE);
  void set_ellipse(int x, int y, int w, int h, SelectionCombineMode mode = SelectionCombineMode::REPLACE);
  void add_polygon(const std::vector<std::pair<double, double>> &polygon_points,
                   const TiledImage *source_image = nullptr,
                   bool magic_catch = false,
                   SelectionCombineMode mode = SelectionCombineMode::REPLACE);
  void set_from_tiled_image(const TiledImage &image, SelectionCombineMode mode = SelectionCombineMode::REPLACE);
  void invert();
  void clear();
  void select_all();
  void translate(int delta_x, int delta_y);
  void scale(int new_width, int new_height);
  void transform(double pivot_x, double pivot_y, double offset_x, double offset_y,
                 double rotation, double scale_x = 1.0, double scale_y = 1.0);
  void feather(double radius);
  void grow(int pixels);
  void shrink(int pixels);
  void border(int pixels);
  void update_contour();


  const std::vector<std::pair<double, double>> &lasso_points() const {
    return lasso_points_;
  }
  const SelectionRect &bounds() const {
    return bounds_;
  }
  const std::vector<std::uint8_t> &data() const {
    return mask_;
  }

private:
  int width_ = 0;
  int height_ = 0;
  bool active_ = false;
  SelectionRect bounds_{0, 0, 0, 0};
  std::vector<std::uint8_t> mask_;
  std::vector<std::pair<double, double>> lasso_points_;
};

} // namespace kreart::core
