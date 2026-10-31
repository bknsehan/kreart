#pragma once

#include "kreart/core/selection_mask.h"
#include "kreart/core/tiled_image.h"

namespace kreart::core {

struct ColorBalanceTones {
  double cyan_red = 0.0;       // [-100, +100]
  double magenta_green = 0.0;  // [-100, +100]
  double yellow_blue = 0.0;    // [-100, +100]
};

struct ColorBalanceParams {
  ColorBalanceTones shadows;
  ColorBalanceTones midtones;
  ColorBalanceTones highlights;
  bool preserve_luminosity = true;
};

// 1. Hue / Saturation / Value adjustment
// hue_shift_deg: [-180.0, +180.0]
// saturation_scale: [-1.0, +1.0] (-1.0 = full desaturate, 0.0 = original, +1.0 = 2x saturation)
// value_scale: [-1.0, +1.0] (-1.0 = black, 0.0 = original, +1.0 = full bright)
void filter_hsv(TiledImage &image, double hue_shift_deg, double saturation_scale,
                double value_scale, const SelectionMask *selection = nullptr);

// 2. Brightness & Contrast adjustment
// brightness: [-100.0, +100.0]
// contrast: [-100.0, +100.0]
void filter_brightness_contrast(TiledImage &image, double brightness, double contrast,
                                const SelectionMask *selection = nullptr);

// 3. Color Balance adjustment (Shadows, Midtones, Highlights)
void filter_color_balance(TiledImage &image, const ColorBalanceParams &params,
                          const SelectionMask *selection = nullptr);

// 4. Gaussian Blur
// radius: [0.5, 100.0] pixels
void filter_gaussian_blur(TiledImage &image, double radius,
                          const SelectionMask *selection = nullptr);

} // namespace kreart::core
