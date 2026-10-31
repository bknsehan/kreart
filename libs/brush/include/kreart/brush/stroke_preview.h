#pragma once

#include <cstdint>
#include <vector>

#include "kreart/brush/round_brush.h"
#include "kreart/core/tiled_image.h"

namespace kreart::brush {

struct StrokePreviewOptions {
  int width = 260;
  int height = 44;
  core::Rgba8 color{220, 225, 235, 255};
  bool transparent_background = true;
  core::Rgba8 background_color{20, 21, 24, 255};
};

class StrokePreviewGenerator {
public:
  // Renders a synthetic S-curve ribbon preview for a given brush setting
  // into an RGBA8 buffer of size (width * height * 4).
  static std::vector<std::uint8_t> render_preview_rgba(
      const Settings &brush_settings,
      const StrokePreviewOptions &options = {});

  // Renders directly into a TiledImage
  static void render_preview(
      core::TiledImage &image,
      const Settings &brush_settings,
      const StrokePreviewOptions &options = {});
};

} // namespace kreart::brush
