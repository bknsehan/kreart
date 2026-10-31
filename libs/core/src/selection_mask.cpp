#include "kreart/core/selection_mask.h"

#include "kreart/core/tiled_image.h"
#include <algorithm>
#include <cmath>

namespace {

void douglas_peucker(const std::vector<std::pair<double, double>> &pts,
                     std::size_t start, std::size_t end, double epsilon,
                     std::vector<bool> &keep) {
  if (end <= start + 1) return;
  double max_dist = 0.0;
  std::size_t max_idx = start;
  const double x1 = pts[start].first, y1 = pts[start].second;
  const double x2 = pts[end].first, y2 = pts[end].second;
  const double dx = x2 - x1, dy = y2 - y1;
  const double len_sq = dx * dx + dy * dy;

  for (std::size_t i = start + 1; i < end; ++i) {
    double dist = 0.0;
    if (len_sq < 1e-6) {
      dist = std::hypot(pts[i].first - x1, pts[i].second - y1);
    } else {
      const double t = ((pts[i].first - x1) * dx + (pts[i].second - y1) * dy) / len_sq;
      const double proj_x = x1 + t * dx;
      const double proj_y = y1 + t * dy;
      dist = std::hypot(pts[i].first - proj_x, pts[i].second - proj_y);
    }
    if (dist > max_dist) {
      max_dist = dist;
      max_idx = i;
    }
  }

  if (max_dist > epsilon) {
    keep[max_idx] = true;
    douglas_peucker(pts, start, max_idx, epsilon, keep);
    douglas_peucker(pts, max_idx, end, epsilon, keep);
  }
}

void trace_mask_boundary(const std::vector<std::uint8_t> &mask, int width, int height,
                         const kreart::core::SelectionRect &b,
                         std::vector<std::pair<double, double>> &out_contour) {
  out_contour.clear();
  if (b.width <= 0 || b.height <= 0 || mask.empty()) return;

  int start_x = -1, start_y = -1;
  for (int y = b.y; y < b.y + b.height && y < height; ++y) {
    const std::size_t row_idx = static_cast<std::size_t>(y) * width;
    for (int x = b.x; x < b.x + b.width && x < width; ++x) {
      if (mask[row_idx + x] > 0) {
        start_x = x;
        start_y = y;
        break;
      }
    }
    if (start_x != -1) break;
  }

  if (start_x == -1) return;

  constexpr int kDX[4] = {1, 0, -1, 0};
  constexpr int kDY[4] = {0, 1, 0, -1};
  std::vector<std::pair<double, double>> raw_contour;
  int curr_x = start_x;
  int curr_y = start_y;
  int dir = 0;
  raw_contour.push_back({static_cast<double>(curr_x), static_cast<double>(curr_y)});

  const std::size_t max_steps = static_cast<std::size_t>(b.width * b.height * 4 + 100);
  for (std::size_t step = 0; step < max_steps; ++step) {
    int next_dir = -1;
    int next_x = -1, next_y = -1;
    for (int i = 3; i <= 6; ++i) {
      const int d = (dir + i) % 4;
      const int nx = curr_x + kDX[d];
      const int ny = curr_y + kDY[d];
      if (nx >= b.x && nx < b.x + b.width && nx < width &&
          ny >= b.y && ny < b.y + b.height && ny < height &&
          mask[static_cast<std::size_t>(ny) * width + nx] > 0) {
        next_dir = d;
        next_x = nx;
        next_y = ny;
        break;
      }
    }
    if (next_dir == -1 || (step > 1 && next_x == start_x && next_y == start_y)) {
      break;
    }
    raw_contour.push_back({static_cast<double>(next_x), static_cast<double>(next_y)});
    curr_x = next_x;
    curr_y = next_y;
    dir = next_dir;
  }

  if (raw_contour.size() >= 3) {
    std::vector<bool> keep(raw_contour.size(), false);
    keep.front() = true;
    keep.back() = true;
    douglas_peucker(raw_contour, 0, raw_contour.size() - 1, 1.2, keep);

    for (std::size_t i = 0; i < raw_contour.size(); ++i) {
      if (keep[i]) {
        out_contour.push_back(raw_contour[i]);
      }
    }
  }
}

} // namespace

namespace kreart::core {

SelectionMask::SelectionMask(int width, int height) {
  reset(width, height);
}

void SelectionMask::reset(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  clear();
}

void SelectionMask::clear() {
  active_ = false;
  bounds_ = SelectionRect{0, 0, 0, 0};
  mask_.clear();
  lasso_points_.clear();
}

void SelectionMask::select_all() {
  active_ = true;
  bounds_ = SelectionRect{0, 0, width_, height_};
  mask_.assign(static_cast<std::size_t>(width_) * height_, 255);
  lasso_points_ = {
      {0.0, 0.0},
      {static_cast<double>(width_), 0.0},
      {static_cast<double>(width_), static_cast<double>(height_)},
      {0.0, static_cast<double>(height_)}
  };
}

void SelectionMask::invert() {
  if (!active_) {
    select_all();
    return;
  }
  if (mask_.empty()) {
    mask_.assign(static_cast<std::size_t>(width_) * height_, 0);
  }
  bool any_nonzero = false;
  for (auto &byte : mask_) {
    byte = 255 - byte;
    if (byte > 0) {
      any_nonzero = true;
    }
  }
  if (!any_nonzero) {
    clear();
    return;
  }
  bounds_ = SelectionRect{0, 0, width_, height_};
  lasso_points_.clear();
}

void SelectionMask::apply_combine_mask(const std::vector<std::uint8_t> &incoming,
                                       const SelectionRect &incoming_bounds,
                                       SelectionCombineMode mode) {
  if (mode == SelectionCombineMode::REPLACE || !active_) {
    if (mode == SelectionCombineMode::SUBTRACT || mode == SelectionCombineMode::INTERSECT) {
      clear();
      return;
    }
    mask_ = incoming;
    bounds_ = incoming_bounds;
    active_ = (bounds_.width > 0 && bounds_.height > 0);
    if (!active_) {
      clear();
    }
    return;
  }

  lasso_points_.clear();

  if (mask_.size() != static_cast<std::size_t>(width_) * height_) {
    mask_.assign(static_cast<std::size_t>(width_) * height_, 0);
  }

  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  for (int y = 0; y < height_; ++y) {
    const std::size_t row_idx = static_cast<std::size_t>(y) * width_;
    for (int x = 0; x < width_; ++x) {
      const std::size_t idx = row_idx + x;
      const std::uint8_t cur = mask_[idx];
      const std::uint8_t inc = idx < incoming.size() ? incoming[idx] : 0;
      std::uint8_t res = 0;
      if (mode == SelectionCombineMode::ADD) {
        res = std::max(cur, inc);
      } else if (mode == SelectionCombineMode::SUBTRACT) {
        res = static_cast<std::uint8_t>(std::max(0, static_cast<int>(cur) - static_cast<int>(inc)));
      } else if (mode == SelectionCombineMode::INTERSECT) {
        res = static_cast<std::uint8_t>((static_cast<int>(cur) * static_cast<int>(inc)) / 255);
      }
      mask_[idx] = res;
      if (res > 0) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
    }
  }

  if (max_x >= min_x && max_y >= min_y) {
    active_ = true;
    bounds_.x = min_x;
    bounds_.y = min_y;
    bounds_.width = max_x - min_x + 1;
    bounds_.height = max_y - min_y + 1;
  } else {
    clear();
  }
}

void SelectionMask::set_rect(int x, int y, int w, int h, SelectionCombineMode mode) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  x = std::clamp(x, 0, width_);
  y = std::clamp(y, 0, height_);
  w = std::clamp(w, 0, width_ - x);
  h = std::clamp(h, 0, height_ - y);

  if (w <= 0 || h <= 0) {
    if (mode == SelectionCombineMode::REPLACE) {
      clear();
    }
    return;
  }

  std::vector<std::uint8_t> incoming(static_cast<std::size_t>(width_) * height_, 0);
  for (int row = y; row < y + h; ++row) {
    std::fill_n(incoming.data() + static_cast<std::size_t>(row) * width_ + x, w, 255);
  }

  SelectionRect incoming_bounds{x, y, w, h};
  apply_combine_mask(incoming, incoming_bounds, mode);

  if (mode == SelectionCombineMode::REPLACE && active_) {
    lasso_points_ = {
        {static_cast<double>(x), static_cast<double>(y)},
        {static_cast<double>(x + w), static_cast<double>(y)},
        {static_cast<double>(x + w), static_cast<double>(y + h)},
        {static_cast<double>(x), static_cast<double>(y + h)}
    };
  }
}

void SelectionMask::set_ellipse(int x, int y, int w, int h, SelectionCombineMode mode) {
  if (w < 0) {
    x += w;
    w = -w;
  }
  if (h < 0) {
    y += h;
    h = -h;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  x = std::clamp(x, 0, width_);
  y = std::clamp(y, 0, height_);
  w = std::clamp(w, 0, width_ - x);
  h = std::clamp(h, 0, height_ - y);

  if (w <= 2 || h <= 2) {
    if (mode == SelectionCombineMode::REPLACE) {
      clear();
    }
    return;
  }

  const double cx = x + w / 2.0;
  const double cy = y + h / 2.0;
  const double rx = w / 2.0;
  const double ry = h / 2.0;
  if (rx < 1e-4 || ry < 1e-4) {
    if (mode == SelectionCombineMode::REPLACE) {
      clear();
    }
    return;
  }

  const double rx_sq = rx * rx;
  const double ry_sq = ry * ry;

  std::vector<std::uint8_t> incoming(static_cast<std::size_t>(width_) * height_, 0);
  int min_ex = width_, max_ex = -1;
  int min_ey = height_, max_ey = -1;

  for (int py = y; py < y + h; ++py) {
    const double dy = (py + 0.5) - cy;
    const double dy_sq = dy * dy;
    for (int px = x; px < x + w; ++px) {
      const double dx = (px + 0.5) - cx;
      if ((dx * dx) / rx_sq + dy_sq / ry_sq <= 1.0) {
        incoming[static_cast<std::size_t>(py) * width_ + px] = 255;
        min_ex = std::min(min_ex, px);
        max_ex = std::max(max_ex, px);
        min_ey = std::min(min_ey, py);
        max_ey = std::max(max_ey, py);
      }
    }
  }

  if (max_ex < min_ex || max_ey < min_ey) {
    if (mode == SelectionCombineMode::REPLACE) {
      clear();
    }
    return;
  }

  SelectionRect incoming_bounds{min_ex, min_ey, max_ex - min_ex + 1, max_ey - min_ey + 1};
  apply_combine_mask(incoming, incoming_bounds, mode);
  if (active_) {
    lasso_points_.clear();
    constexpr int kSteps = 64;
    for (int i = 0; i < kSteps; ++i) {
      const double theta = (2.0 * std::numbers::pi * i) / kSteps;
      lasso_points_.emplace_back(cx + rx * std::cos(theta), cy + ry * std::sin(theta));
    }
  }
}

void SelectionMask::set_from_tiled_image(const TiledImage &image, SelectionCombineMode mode) {
  std::vector<std::uint8_t> incoming(static_cast<std::size_t>(width_) * height_, 0);
  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  for (const auto &[key, tile] : image.tiles()) {
    const int tx = TiledImage::key_x(key);
    const int ty = TiledImage::key_y(key);
    for (int ly = 0; ly < kTileSize; ++ly) {
      const int doc_y = ty * kTileSize + ly;
      if (doc_y >= height_) break;
      for (int lx = 0; lx < kTileSize; ++lx) {
        const int doc_x = tx * kTileSize + lx;
        if (doc_x >= width_) break;
        const std::uint8_t a = tile->pixels[ly * kTileSize + lx].a;
        if (a > 0) {
          incoming[static_cast<std::size_t>(doc_y) * width_ + doc_x] = a;
          min_x = std::min(min_x, doc_x);
          max_x = std::max(max_x, doc_x);
          min_y = std::min(min_y, doc_y);
          max_y = std::max(max_y, doc_y);
        }
      }
    }
  }

  SelectionRect incoming_bounds{};
  if (max_x >= min_x && max_y >= min_y) {
    incoming_bounds = SelectionRect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
  }

  apply_combine_mask(incoming, incoming_bounds, mode);
  // Raster masks (Selection Layer) can have arbitrary disconnected islands
  // and inner holes. Leaving lasso_points_ empty tells the canvas renderer to
  // draw exact pixel-boundary marching ants around all shapes and holes.
  lasso_points_.clear();
}

void SelectionMask::update_contour() {
  if (!active_ || bounds_.width <= 0 || bounds_.height <= 0 || mask_.empty()) {
    lasso_points_.clear();
    return;
  }
  trace_mask_boundary(mask_, width_, height_, bounds_, lasso_points_);
}

void SelectionMask::add_polygon(const std::vector<std::pair<double, double>> &polygon_points,
                                const TiledImage *source_image, bool magic_catch,
                                SelectionCombineMode mode) {
  if (polygon_points.size() < 3) {
    if (mode == SelectionCombineMode::REPLACE) {
      clear();
    }
    return;
  }

  double min_x = polygon_points[0].first;
  double max_x = min_x;
  double min_y = polygon_points[0].second;
  double max_y = min_y;

  for (const auto &pt : polygon_points) {
    min_x = std::min(min_x, pt.first);
    max_x = std::max(max_x, pt.first);
    min_y = std::min(min_y, pt.second);
    max_y = std::max(max_y, pt.second);
  }

  const int b_x = std::clamp(static_cast<int>(std::floor(min_x)), 0, width_);
  const int b_y = std::clamp(static_cast<int>(std::floor(min_y)), 0, height_);
  const int b_w = std::clamp(static_cast<int>(std::ceil(max_x)) - b_x, 0, width_ - b_x);
  const int b_h = std::clamp(static_cast<int>(std::ceil(max_y)) - b_y, 0, height_ - b_y);

  if (b_w <= 0 || b_h <= 0) {
    if (mode == SelectionCombineMode::REPLACE) {
      clear();
    }
    return;
  }

  std::vector<std::uint8_t> incoming(static_cast<std::size_t>(width_) * height_, 0);

  const std::size_t n = polygon_points.size();
  for (int y = b_y; y < b_y + b_h; ++y) {
    const double scan_y = y + 0.5;
    std::vector<double> node_x;

    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
      const double y_i = polygon_points[i].second;
      const double y_j = polygon_points[j].second;
      if ((y_i < scan_y && y_j >= scan_y) || (y_j < scan_y && y_i >= scan_y)) {
        const double x_i = polygon_points[i].first;
        const double x_j = polygon_points[j].first;
        const double intersect_x = x_i + (scan_y - y_i) / (y_j - y_i) * (x_j - x_i);
        node_x.push_back(intersect_x);
      }
    }

    std::sort(node_x.begin(), node_x.end());

    for (std::size_t k = 0; k + 1 < node_x.size(); k += 2) {
      const int start_x = std::clamp(static_cast<int>(std::ceil(node_x[k])), 0, width_);
      const int end_x = std::clamp(static_cast<int>(std::floor(node_x[k + 1])), 0, width_ - 1);
      if (end_x >= start_x) {
        std::fill_n(incoming.data() + static_cast<std::size_t>(y) * width_ + start_x,
                    end_x - start_x + 1, 255);
      }
    }
  }

  SelectionRect incoming_bounds{b_x, b_y, b_w, b_h};
  std::vector<std::pair<double, double>> final_contour = polygon_points;

  if (magic_catch && source_image != nullptr) {
    bool has_content = false;
    for (int y = b_y; y < b_y + b_h; ++y) {
      for (int x = b_x; x < b_x + b_w; ++x) {
        if (incoming[static_cast<std::size_t>(y) * width_ + x] > 0) {
          if (source_image->pixel(x, y).a > 10) {
            has_content = true;
            break;
          }
        }
      }
      if (has_content) break;
    }

    if (has_content) {
      std::vector<std::uint8_t> grid(static_cast<std::size_t>(b_w) * b_h, 0);
      std::vector<std::pair<int, int>> queue;

      for (int ly = 0; ly < b_h; ++ly) {
        const int gy = b_y + ly;
        for (int lx = 0; lx < b_w; ++lx) {
          const int gx = b_x + lx;
          const std::size_t g_idx = static_cast<std::size_t>(ly) * b_w + lx;
          if (incoming[static_cast<std::size_t>(gy) * width_ + gx] > 0) {
            if (source_image->pixel(gx, gy).a > 10) {
              grid[g_idx] = 1;
            } else {
              grid[g_idx] = 2;
              if (lx == 0 || ly == 0 || lx == b_w - 1 || ly == b_h - 1) {
                grid[g_idx] = 0;
                queue.push_back({lx, ly});
              }
            }
          } else {
            grid[g_idx] = 0;
            queue.push_back({lx, ly});
          }
        }
      }

      std::size_t head = 0;
      while (head < queue.size()) {
        const auto [cx, cy] = queue[head++];
        constexpr int dx[4] = {-1, 1, 0, 0};
        constexpr int dy[4] = {0, 0, -1, 1};
        for (int d = 0; d < 4; ++d) {
          const int nx = cx + dx[d];
          const int ny = cy + dy[d];
          if (nx >= 0 && nx < b_w && ny >= 0 && ny < b_h) {
            const std::size_t n_idx = static_cast<std::size_t>(ny) * b_w + nx;
            if (grid[n_idx] == 2) {
              grid[n_idx] = 0;
              queue.push_back({nx, ny});
            }
          }
        }
      }

      int min_cx = width_, max_cx = -1;
      int min_cy = height_, max_cy = -1;

      for (int ly = 0; ly < b_h; ++ly) {
        const int gy = b_y + ly;
        for (int lx = 0; lx < b_w; ++lx) {
          const int gx = b_x + lx;
          const std::size_t g_idx = static_cast<std::size_t>(ly) * b_w + lx;
          const std::size_t m_idx = static_cast<std::size_t>(gy) * width_ + gx;
          if (grid[g_idx] > 0) {
            incoming[m_idx] = 255;
            min_cx = std::min(min_cx, gx);
            max_cx = std::max(max_cx, gx);
            min_cy = std::min(min_cy, gy);
            max_cy = std::max(max_cy, gy);
          } else {
            incoming[m_idx] = 0;
          }
        }
      }

      if (max_cx >= min_cx && max_cy >= min_cy) {
        incoming_bounds = SelectionRect{min_cx, min_cy, max_cx - min_cx + 1, max_cy - min_cy + 1};

        int start_x = -1, start_y = -1;
        for (int ly = 0; ly < b_h; ++ly) {
          for (int lx = 0; lx < b_w; ++lx) {
            if (grid[static_cast<std::size_t>(ly) * b_w + lx] > 0) {
              start_x = lx;
              start_y = ly;
              break;
            }
          }
          if (start_x != -1) break;
        }

        if (start_x != -1) {
          constexpr int kDX[4] = {1, 0, -1, 0};
          constexpr int kDY[4] = {0, 1, 0, -1};
          std::vector<std::pair<double, double>> raw_contour;
          int curr_x = start_x;
          int curr_y = start_y;
          int dir = 0;
          raw_contour.push_back({static_cast<double>(b_x + curr_x), static_cast<double>(b_y + curr_y)});

          const std::size_t max_steps = static_cast<std::size_t>(b_w * b_h * 4 + 100);
          for (std::size_t step = 0; step < max_steps; ++step) {
            int next_dir = -1;
            int next_x = -1, next_y = -1;
            for (int i = 3; i <= 6; ++i) {
              const int d = (dir + i) % 4;
              const int nx = curr_x + kDX[d];
              const int ny = curr_y + kDY[d];
              if (nx >= 0 && nx < b_w && ny >= 0 && ny < b_h &&
                  grid[static_cast<std::size_t>(ny) * b_w + nx] > 0) {
                next_dir = d;
                next_x = nx;
                next_y = ny;
                break;
              }
            }
            if (next_dir == -1 || (step > 1 && next_x == start_x && next_y == start_y)) {
              break;
            }
            raw_contour.push_back({static_cast<double>(b_x + next_x), static_cast<double>(b_y + next_y)});
            curr_x = next_x;
            curr_y = next_y;
            dir = next_dir;
          }
          if (raw_contour.size() >= 3) {
            std::vector<bool> keep(raw_contour.size(), false);
            keep.front() = true;
            keep.back() = true;
            douglas_peucker(raw_contour, 0, raw_contour.size() - 1, 1.2, keep);

            std::vector<std::pair<double, double>> simplified;
            for (std::size_t i = 0; i < raw_contour.size(); ++i) {
              if (keep[i]) {
                simplified.push_back(raw_contour[i]);
              }
            }
            if (simplified.size() >= 3) {
              final_contour = std::move(simplified);
            }
          }
        }
      } else {
        if (mode == SelectionCombineMode::REPLACE) {
          clear();
        }
        return;
      }
    }
  }

  apply_combine_mask(incoming, incoming_bounds, mode);
  if (mode == SelectionCombineMode::REPLACE && active_) {
    lasso_points_ = std::move(final_contour);
  }
}
void SelectionMask::translate(int delta_x, int delta_y) {
  if (!active_ || (delta_x == 0 && delta_y == 0)) {
    return;
  }

  bounds_.x = std::clamp(bounds_.x + delta_x, 0, width_);
  bounds_.y = std::clamp(bounds_.y + delta_y, 0, height_);
  bounds_.width = std::clamp(bounds_.width, 0, width_ - bounds_.x);
  bounds_.height = std::clamp(bounds_.height, 0, height_ - bounds_.y);

  for (auto &pt : lasso_points_) {
    pt.first += delta_x;
    pt.second += delta_y;
  }

  if (!mask_.empty()) {
    std::vector<std::uint8_t> new_mask(static_cast<std::size_t>(width_) * height_, 0);
    for (int y = 0; y < height_; ++y) {
      const int old_y = y - delta_y;
      if (old_y < 0 || old_y >= height_) continue;
      for (int x = 0; x < width_; ++x) {
        const int old_x = x - delta_x;
        if (old_x < 0 || old_x >= width_) continue;
        new_mask[static_cast<std::size_t>(y) * width_ + x] =
            mask_[static_cast<std::size_t>(old_y) * width_ + old_x];
      }
    }
    mask_ = std::move(new_mask);
  }
}
void SelectionMask::transform(double pivot_x, double pivot_y, double offset_x, double offset_y,
                              double rotation, double scale_x, double scale_y) {
  if (!active_) {
    return;
  }
  if (std::abs(rotation) < 1e-6 && std::abs(scale_x - 1.0) < 1e-6 && std::abs(scale_y - 1.0) < 1e-6) {
    translate(static_cast<int>(std::round(offset_x)), static_cast<int>(std::round(offset_y)));
    return;
  }

  const double cos_r = std::cos(rotation);
  const double sin_r = std::sin(rotation);

  if (!lasso_points_.empty()) {
    for (auto &pt : lasso_points_) {
      const double rel_x = (pt.first - pivot_x) * scale_x;
      const double rel_y = (pt.second - pivot_y) * scale_y;
      pt.first = pivot_x + offset_x + cos_r * rel_x - sin_r * rel_y;
      pt.second = pivot_y + offset_y + sin_r * rel_x + cos_r * rel_y;
    }
  } else if (bounds_.width > 0 && bounds_.height > 0) {
    const double x0 = bounds_.x;
    const double y0 = bounds_.y;
    const double x1 = bounds_.x + bounds_.width;
    const double y1 = bounds_.y + bounds_.height;
    std::vector<std::pair<double, double>> corners = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (auto &pt : corners) {
      const double rel_x = (pt.first - pivot_x) * scale_x;
      const double rel_y = (pt.second - pivot_y) * scale_y;
      pt.first = pivot_x + offset_x + cos_r * rel_x - sin_r * rel_y;
      pt.second = pivot_y + offset_y + sin_r * rel_x + cos_r * rel_y;
    }
    lasso_points_ = std::move(corners);
  }

  if (!mask_.empty()) {
    std::vector<std::uint8_t> new_mask(static_cast<std::size_t>(width_) * height_, 0);
    int min_x = width_, max_x = -1;
    int min_y = height_, max_y = -1;

    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        const double dx = x - (pivot_x + offset_x);
        const double dy = y - (pivot_y + offset_y);
        const double rot_x = cos_r * dx + sin_r * dy;
        const double rot_y = -sin_r * dx + cos_r * dy;
        const double ox = pivot_x + rot_x / scale_x;
        const double oy = pivot_y + rot_y / scale_y;

        const int ix = static_cast<int>(std::round(ox));
        const int iy = static_cast<int>(std::round(oy));
        if (ix >= 0 && ix < width_ && iy >= 0 && iy < height_) {
          const std::uint8_t val = mask_[static_cast<std::size_t>(iy) * width_ + ix];
          if (val > 0) {
            new_mask[static_cast<std::size_t>(y) * width_ + x] = val;
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
          }
        }
      }
    }
    mask_ = std::move(new_mask);
    if (max_x >= min_x && max_y >= min_y) {
      bounds_.x = min_x;
      bounds_.y = min_y;
      bounds_.width = max_x - min_x + 1;
      bounds_.height = max_y - min_y + 1;
    } else {
      clear();
    }
  }
}
void SelectionMask::scale(int new_width, int new_height) {
  new_width = std::max(1, new_width);
  new_height = std::max(1, new_height);
  if (!active_ || mask_.empty()) {
    reset(new_width, new_height);
    return;
  }

  const double sx = static_cast<double>(new_width) / width_;
  const double sy = static_cast<double>(new_height) / height_;

  bounds_.x = std::clamp(static_cast<int>(std::round(bounds_.x * sx)), 0, new_width - 1);
  bounds_.y = std::clamp(static_cast<int>(std::round(bounds_.y * sy)), 0, new_height - 1);
  bounds_.width = std::clamp(static_cast<int>(std::round(bounds_.width * sx)), 1, new_width - bounds_.x);
  bounds_.height = std::clamp(static_cast<int>(std::round(bounds_.height * sy)), 1, new_height - bounds_.y);

  for (auto &pt : lasso_points_) {
    pt.first *= sx;
    pt.second *= sy;
  }

  std::vector<std::uint8_t> new_mask(static_cast<std::size_t>(new_width) * new_height, 0);
  for (int y = 0; y < new_height; ++y) {
    const int old_y = std::clamp(static_cast<int>(std::floor(y / sy)), 0, height_ - 1);
    for (int x = 0; x < new_width; ++x) {
      const int old_x = std::clamp(static_cast<int>(std::floor(x / sx)), 0, width_ - 1);
      new_mask[static_cast<std::size_t>(y) * new_width + x] =
          mask_[static_cast<std::size_t>(old_y) * width_ + old_x];
    }
  }

  width_ = new_width;
  height_ = new_height;
  mask_ = std::move(new_mask);
}

void SelectionMask::feather(double radius) {
  if (!active_ || mask_.empty() || radius <= 0.1) {
    return;
  }
  lasso_points_.clear();

  const int r = std::clamp(static_cast<int>(std::ceil(radius * 2.0)), 1, 100);
  const double sigma = radius * 0.5;
  const double two_sigma_sq = 2.0 * sigma * sigma;

  std::vector<double> kernel(2 * r + 1);
  double ksum = 0.0;
  for (int d = -r; d <= r; ++d) {
    double w = std::exp(-(d * d) / two_sigma_sq);
    kernel[d + r] = w;
    ksum += w;
  }
  for (double &w : kernel) {
    w /= ksum;
  }

  // Pass 1: Horizontal 1D convolution
  std::vector<double> temp(static_cast<std::size_t>(width_) * height_, 0.0);
  for (int y = 0; y < height_; ++y) {
    const std::size_t row_idx = static_cast<std::size_t>(y) * width_;
    for (int x = 0; x < width_; ++x) {
      double acc = 0.0;
      for (int d = -r; d <= r; ++d) {
        const int sx = std::clamp(x + d, 0, width_ - 1);
        acc += mask_[row_idx + sx] * kernel[d + r];
      }
      temp[row_idx + x] = acc;
    }
  }

  // Pass 2: Vertical 1D convolution
  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      double acc = 0.0;
      for (int d = -r; d <= r; ++d) {
        const int sy = std::clamp(y + d, 0, height_ - 1);
        acc += temp[static_cast<std::size_t>(sy) * width_ + x] * kernel[d + r];
      }
      const auto val = static_cast<std::uint8_t>(std::clamp(std::round(acc), 0.0, 255.0));
      mask_[static_cast<std::size_t>(y) * width_ + x] = val;
      if (val > 0) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
    }
  }

  if (max_x >= min_x && max_y >= min_y) {
    bounds_.x = min_x;
    bounds_.y = min_y;
    bounds_.width = max_x - min_x + 1;
    bounds_.height = max_y - min_y + 1;
    active_ = true;
  } else {
    clear();
  }
}

void SelectionMask::grow(int pixels) {
  if (!active_ || mask_.empty() || pixels <= 0) {
    return;
  }
  lasso_points_.clear();

  pixels = std::clamp(pixels, 1, 200);
  const int rad_sq = pixels * pixels;
  std::vector<std::pair<int, int>> offsets;
  for (int dy = -pixels; dy <= pixels; ++dy) {
    for (int dx = -pixels; dx <= pixels; ++dx) {
      if (dx * dx + dy * dy <= rad_sq) {
        offsets.push_back({dx, dy});
      }
    }
  }

  std::vector<std::uint8_t> new_mask = mask_;
  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      const std::uint8_t cur = mask_[static_cast<std::size_t>(y) * width_ + x];
      if (cur > 0) {
        for (const auto &[dx, dy] : offsets) {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
            const std::size_t nidx = static_cast<std::size_t>(ny) * width_ + nx;
            new_mask[nidx] = std::max(new_mask[nidx], cur);
          }
        }
      }
    }
  }

  mask_ = std::move(new_mask);
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      if (mask_[static_cast<std::size_t>(y) * width_ + x] > 0) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
    }
  }

  if (max_x >= min_x && max_y >= min_y) {
    bounds_.x = min_x;
    bounds_.y = min_y;
    bounds_.width = max_x - min_x + 1;
    bounds_.height = max_y - min_y + 1;
    active_ = true;
  } else {
    clear();
  }
}

void SelectionMask::shrink(int pixels) {
  if (!active_ || mask_.empty() || pixels <= 0) {
    return;
  }
  lasso_points_.clear();

  pixels = std::clamp(pixels, 1, 200);
  const int rad_sq = pixels * pixels;
  std::vector<std::pair<int, int>> offsets;
  for (int dy = -pixels; dy <= pixels; ++dy) {
    for (int dx = -pixels; dx <= pixels; ++dx) {
      if (dx * dx + dy * dy <= rad_sq) {
        offsets.push_back({dx, dy});
      }
    }
  }

  std::vector<std::uint8_t> new_mask(static_cast<std::size_t>(width_) * height_, 0);
  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      const std::uint8_t cur = mask_[static_cast<std::size_t>(y) * width_ + x];
      if (cur == 0) continue;

      std::uint8_t min_val = cur;
      for (const auto &[dx, dy] : offsets) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) {
          min_val = 0;
          break;
        }
        min_val = std::min(min_val, mask_[static_cast<std::size_t>(ny) * width_ + nx]);
        if (min_val == 0) break;
      }

      new_mask[static_cast<std::size_t>(y) * width_ + x] = min_val;
      if (min_val > 0) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
    }
  }

  mask_ = std::move(new_mask);
  if (max_x >= min_x && max_y >= min_y) {
    bounds_.x = min_x;
    bounds_.y = min_y;
    bounds_.width = max_x - min_x + 1;
    bounds_.height = max_y - min_y + 1;
    active_ = true;
  } else {
    clear();
  }
}

void SelectionMask::border(int pixels) {
  if (!active_ || mask_.empty() || pixels <= 0) {
    return;
  }
  SelectionMask grown = *this;
  grown.grow(pixels);
  SelectionMask shrunk = *this;
  shrunk.shrink(pixels);

  lasso_points_.clear();
  int min_x = width_, max_x = -1;
  int min_y = height_, max_y = -1;

  for (std::size_t i = 0; i < mask_.size(); ++i) {
    const std::uint8_t g = i < grown.data().size() ? grown.data()[i] : 0;
    const std::uint8_t s = i < shrunk.data().size() ? shrunk.data()[i] : 0;
    const auto val = static_cast<std::uint8_t>(std::max(0, static_cast<int>(g) - static_cast<int>(s)));
    mask_[i] = val;
    if (val > 0) {
      const int x = static_cast<int>(i % width_);
      const int y = static_cast<int>(i / width_);
      min_x = std::min(min_x, x);
      max_x = std::max(max_x, x);
      min_y = std::min(min_y, y);
      max_y = std::max(max_y, y);
    }
  }

  if (max_x >= min_x && max_y >= min_y) {
    bounds_.x = min_x;
    bounds_.y = min_y;
    bounds_.width = max_x - min_x + 1;
    bounds_.height = max_y - min_y + 1;
    active_ = true;
  } else {
    clear();
  }
}



} // namespace kreart::core
