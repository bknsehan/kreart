#pragma once

#include <QPixmap>
#include <memory>
#include <string>
#include <unordered_map>
#include "kreart/resources/preset.h"
#include "kreart/resources/resource_database.h"

namespace kreart::ui {

class StrokePreviewCache {
public:
  StrokePreviewCache() = default;

  // Retrieves or generates a cached QPixmap of the stroke preview ribbon
  QPixmap get_preview(
      const resources::Preset &preset,
      const resources::ResourceDatabase *database,
      bool is_dark_theme,
      int width = 240,
      int height = 36);

  // Clears the cache (called when theme changes)
  void clear();

private:
  std::unordered_map<std::string, QPixmap> cache_;
};

} // namespace kreart::ui
