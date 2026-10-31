#include "kreart/brush/stroke_preview.h"

#include <algorithm>
#include <cmath>

namespace kreart::brush {

void StrokePreviewGenerator::render_preview(
    core::TiledImage &image,
    const Settings &brush_settings,
    const StrokePreviewOptions &options) {
  if (!options.transparent_background) {
    image.fill(options.background_color);
  } else {
    image.fill(core::Rgba8{0, 0, 0, 0});
  }

  RoundBrush brush;
  Settings bs = brush_settings;

  // Scale brush diameter to fit neatly within the ribbon preview height
  const double max_allowed_size = static_cast<double>(options.height) * 0.52;
  const double min_allowed_size = 2.5;
  if (bs.size > max_allowed_size) {
    bs.size = max_allowed_size;
  } else if (bs.size < min_allowed_size) {
    bs.size = min_allowed_size;
  }

  bs.color = options.color;

  // If preset is an eraser, render its stroke geometry in a visible preview tint
  if (bs.mode == Mode::ERASE) {
    bs.mode = Mode::PAINT;
    bs.color = core::Rgba8{215, 220, 230, 230};
  }

  // Use direct spline interpolation for deterministic preview generation
  bs.smoothing_mode = SmoothingMode::NONE;
  brush.set_settings(bs);

  const int num_steps = 72;
  const double pad_x = 14.0;
  const double usable_w = static_cast<double>(options.width) - (pad_x * 2.0);
  const double mid_y = static_cast<double>(options.height) / 2.0;
  const double amplitude_y = static_cast<double>(options.height) * 0.16;

  for (int i = 0; i < num_steps; ++i) {
    const double t = static_cast<double>(i) / (num_steps - 1.0);
    const double x = pad_x + t * usable_w;
    const double y = mid_y + std::sin(t * 2.0 * M_PI) * amplitude_y;

    // Organic pressure envelope: delicate entry hairline -> rich body -> fine taper tail
    const double sin_t = std::sin(t * M_PI);
    const double pressure = std::clamp(std::pow(sin_t, 1.25), 0.04, 1.0);

    Point pt{x, y};
    if (i == 0) {
      brush.begin(image, pt, pressure, 0.0, 0.0);
    } else {
      brush.move_to(image, pt, pressure, 0.0, 0.0);
    }
  }
  brush.end(image);
}

std::vector<std::uint8_t> StrokePreviewGenerator::render_preview_rgba(
    const Settings &brush_settings,
    const StrokePreviewOptions &options) {
  core::TiledImage image(options.width, options.height);
  render_preview(image, brush_settings, options);

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(options.width * options.height * 4), 0);
  for (int y = 0; y < options.height; ++y) {
    for (int x = 0; x < options.width; ++x) {
      const core::Rgba8 px = image.pixel(x, y);
      const std::size_t idx = static_cast<std::size_t>((y * options.width + x) * 4);
      rgba[idx + 0] = px.r;
      rgba[idx + 1] = px.g;
      rgba[idx + 2] = px.b;
      rgba[idx + 3] = px.a;
    }
  }

  return rgba;
}

} // namespace kreart::brush
