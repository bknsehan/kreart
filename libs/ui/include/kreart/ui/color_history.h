#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "kreart/core/tiled_image.h"

namespace kreart::ui {

struct PaletteProfile {
  std::string name;
  std::vector<core::Rgba8> colors;
};

class ColorHistory {
public:
  static constexpr std::size_t kMaxColors = 8;

  void record(core::Rgba8 color);
  void clear();

  const std::vector<core::Rgba8> &colors() const {
    return colors_;
  }
  static const std::vector<PaletteProfile> &palette_profiles();
  static const std::vector<core::Rgba8> &default_palette();

  std::size_t active_profile_index() const {
    return active_profile_index_;
  }
  void set_active_profile_index(std::size_t index) {
    if (index < palette_profiles().size()) {
      active_profile_index_ = index;
    }
  }
  const std::vector<core::Rgba8> &active_palette() const {
    const auto &profiles = palette_profiles();
    if (active_profile_index_ < profiles.size()) {
      return profiles[active_profile_index_].colors;
    }
    return default_palette();
  }

private:
  std::vector<core::Rgba8> colors_;
  std::size_t active_profile_index_ = 0;
};

} // namespace kreart::ui
