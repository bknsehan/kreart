#include "kreart/ui/color_history.h"

#include <algorithm>

namespace kreart::ui {

void ColorHistory::record(core::Rgba8 color) {
  const auto same_color = [color](core::Rgba8 candidate) {
    return candidate.r == color.r && candidate.g == color.g && candidate.b == color.b &&
           candidate.a == color.a;
  };

  colors_.erase(std::remove_if(colors_.begin(), colors_.end(), same_color), colors_.end());
  colors_.insert(colors_.begin(), color);
  if (colors_.size() > kMaxColors) {
    colors_.resize(kMaxColors);
  }
}

void ColorHistory::clear() {
  colors_.clear();
}

const std::vector<PaletteProfile> &ColorHistory::palette_profiles() {
  static const std::vector<PaletteProfile> kProfiles = {
      {"Essentials", {
          core::Rgba8{0, 0, 0, 255},       // Black
          core::Rgba8{45, 47, 54, 255},     // Dark Charcoal
          core::Rgba8{140, 142, 148, 255}, // Neutral Gray
          core::Rgba8{255, 255, 255, 255}, // Pure White
          core::Rgba8{245, 205, 175, 255}, // Skin Warm
          core::Rgba8{225, 45, 55, 255},   // Crimson Red
          core::Rgba8{35, 120, 235, 255},  // Cobalt Blue
          core::Rgba8{245, 175, 25, 255}   // Amber / Gold
      }},
      {"Skin & Portrait", {
          core::Rgba8{250, 225, 205, 255}, // Fair Peach
          core::Rgba8{240, 195, 170, 255}, // Rosy Warm
          core::Rgba8{225, 165, 130, 255}, // Tan Midtone
          core::Rgba8{195, 130, 95, 255},  // Shadow Umber
          core::Rgba8{135, 80, 55, 255},   // Deep Contour
          core::Rgba8{220, 100, 110, 255}, // Lip Coral
          core::Rgba8{120, 60, 80, 255},   // Plum Shadow
          core::Rgba8{255, 245, 235, 255}  // Ivory Highlight
      }},
      {"Manga & Inking", {
          core::Rgba8{0, 0, 0, 255},       // Pure Black
          core::Rgba8{50, 50, 55, 255},    // Ink Wash 80%
          core::Rgba8{125, 125, 130, 255}, // Screentone 50%
          core::Rgba8{200, 200, 205, 255}, // Light Wash 20%
          core::Rgba8{255, 255, 255, 255}, // White Correction
          core::Rgba8{65, 140, 240, 255},  // Non-Photo Blue (Sketch)
          core::Rgba8{105, 65, 40, 255},   // Sepia Ink
          core::Rgba8{215, 35, 45, 255}    // Editorial Red
      }},
      {"Landscape & Nature", {
          core::Rgba8{90, 175, 245, 255},  // Sky Blue
          core::Rgba8{205, 225, 240, 255}, // Cloud Mist
          core::Rgba8{45, 145, 65, 255},   // Foliage Green
          core::Rgba8{25, 85, 40, 255},    // Pine Shadow
          core::Rgba8{140, 90, 50, 255},   // Earth Brown
          core::Rgba8{85, 75, 70, 255},    // Stone Gray
          core::Rgba8{255, 220, 100, 255}, // Warm Sunlight
          core::Rgba8{240, 110, 75, 255}   // Sunset Orange
      }},
      {"Anime & Cel", {
          core::Rgba8{30, 25, 45, 255},    // Lineart Dark Indigo
          core::Rgba8{85, 70, 140, 255},   // Ambient Shadow
          core::Rgba8{255, 215, 190, 255}, // Anime Skin
          core::Rgba8{40, 205, 215, 255},  // Hair Cyan
          core::Rgba8{240, 75, 150, 255},  // Anime Pink
          core::Rgba8{250, 220, 50, 255},  // Vibrant Yellow
          core::Rgba8{50, 205, 100, 255},  // Bright Emerald
          core::Rgba8{255, 255, 255, 255}  // Specular White
      }},
      {"Pastel Tones", {
          core::Rgba8{255, 190, 205, 255}, // Pastel Pink
          core::Rgba8{215, 190, 245, 255}, // Lilac
          core::Rgba8{180, 235, 215, 255}, // Mint Green
          core::Rgba8{185, 215, 250, 255}, // Baby Blue
          core::Rgba8{255, 240, 180, 255}, // Buttercup
          core::Rgba8{255, 210, 185, 255}, // Soft Peach
          core::Rgba8{245, 240, 230, 255}, // Cream
          core::Rgba8{200, 175, 190, 255}  // Muted Mauve
      }}
  };
  return kProfiles;
}

const std::vector<core::Rgba8> &ColorHistory::default_palette() {
  return palette_profiles()[0].colors;
}

} // namespace kreart::ui
