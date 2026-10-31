#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kreart::brush {

class TextureMask {
public:
  TextureMask() = default;
  TextureMask(int width, int height, std::vector<std::uint8_t> data, std::string name = "");

  int width() const {
    return width_;
  }
  int height() const {
    return height_;
  }
  const std::string &name() const {
    return name_;
  }
  bool empty() const {
    return data_.empty() || width_ <= 0 || height_ <= 0;
  }

  // Sample normalized texture height [0.0, 1.0] at document pixel coordinate (doc_x, doc_y)
  double sample(double doc_x, double doc_y, double scale = 1.0,
                double offset_x = 0.0, double offset_y = 0.0) const;

  const std::vector<std::uint8_t> &data() const {
    return data_;
  }

  // Loaders
  static std::shared_ptr<TextureMask> load_from_memory(const std::uint8_t *data, std::size_t size,
                                                      std::string name = "");
  static std::shared_ptr<TextureMask> load_pat(const std::uint8_t *data, std::size_t size,
                                               std::string name = "");
  static std::shared_ptr<TextureMask> load_png(const std::uint8_t *data, std::size_t size,
                                               std::string name = "");

  // Procedural artist textures
  static std::shared_ptr<TextureMask> create_rough_paper(int size = 128);
  static std::shared_ptr<TextureMask> create_canvas_grain(int size = 128);
  static std::shared_ptr<TextureMask> create_charcoal_tooth(int size = 128);

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> data_;
  std::string name_;
};

using TextureMaskPtr = std::shared_ptr<TextureMask>;

} // namespace kreart::brush
