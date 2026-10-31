#include "kreart/ui/stroke_preview_cache.h"
#include "kreart/brush/stroke_preview.h"
#include <QImage>

namespace kreart::ui {

QPixmap StrokePreviewCache::get_preview(
    const resources::Preset &preset,
    const resources::ResourceDatabase *database,
    bool is_dark_theme,
    int width,
    int height) {
  const std::string id = preset.path.empty() ? preset.name : preset.path.string();
  const std::string key = id + "_" +
                          (is_dark_theme ? "D" : "L") + "_" +
                          std::to_string(width) + "x" + std::to_string(height);

  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return it->second;
  }

  brush::StrokePreviewOptions opts;
  opts.width = width;
  opts.height = height;
  opts.transparent_background = true;
  if (is_dark_theme) {
    opts.color = core::Rgba8{225, 230, 240, 255};
  } else {
    opts.color = core::Rgba8{24, 28, 36, 255};
  }

  brush::Settings base_settings;
  brush::Settings s = preset.brush_settings(base_settings, database);

  const auto rgba = brush::StrokePreviewGenerator::render_preview_rgba(s, opts);
  if (rgba.empty()) {
    return {};
  }

  QImage img(rgba.data(), width, height, width * 4, QImage::Format_RGBA8888);
  QPixmap pixmap = QPixmap::fromImage(img.copy());

  cache_[key] = pixmap;
  return pixmap;
}

void StrokePreviewCache::clear() {
  cache_.clear();
}

} // namespace kreart::ui
