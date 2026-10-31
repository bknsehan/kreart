#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "kreart/core/tiled_image.h"

namespace kreart::core {

struct StrokeSample {
  double x = 0.0;
  double y = 0.0;
  double pressure = 1.0;
  double tilt_x = 0.0;
  double tilt_y = 0.0;
  std::uint64_t timestamp_ms = 0;
};

struct TrackedBrushSettings {
  int mode = 0; // brush::Mode: 0=PAINT, 1=ERASE, 2=SMUDGE
  double size = 10.0;
  double opacity = 1.0;
  double flow = 1.0;
  double hardness = 0.8;
  double spacing = 0.1;
  std::string composite_op = "normal";
  Rgba8 color{0, 0, 0, 255};
};

enum class DocumentChangeType : std::uint16_t {
  STROKE_COMMITTED = 1,
  LAYER_ADDED = 2,
  LAYER_REMOVED = 3,
  LAYER_MOVED = 4,
  LAYER_PROPERTY = 5,
  SELECTION_TRANSFORM = 6,
  CANVAS_RESIZED = 7,
  FLOOD_FILL = 8,
  FILTER_APPLIED = 9,
  UNDO_STEP = 10,
  REDO_STEP = 11
};

struct DocumentChangeEvent {
  DocumentChangeType type = DocumentChangeType::STROKE_COMMITTED;
  std::size_t layer_id = 0;
  std::uint64_t timestamp_ms = 0;
  TrackedBrushSettings brush_settings;
  std::vector<StrokeSample> stroke_points;
  std::vector<std::uint64_t> modified_tile_keys; // TiledImage keys (tx, ty)
};

} // namespace kreart::core
