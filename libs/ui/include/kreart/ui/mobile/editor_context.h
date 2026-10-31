#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <QString>

#include "kreart/core/document.h"
#include "kreart/ui/canvas.h"
#include "kreart/ui/color_history.h"
#include "kreart/ui/stroke_preview_cache.h"
#include "kreart/resources/resource_database.h"

namespace kreart::ui::mobile {

class MobileHud;
class MobileSheet;

// Everything a sheet needs from the owning editor screen. Passing a context
// keeps the sheet classes free of a dependency on MobileEditorView (no circular
// include) while still letting them drive the document and canvas.
struct EditorContext {
  core::Document *doc = nullptr;
  Canvas *canvas = nullptr;
  ColorHistory *history = nullptr;
  resources::ResourceDatabase *presets = nullptr;
  StrokePreviewCache *previews = nullptr;

  // Applies a colour to the canvas and records it in history.
  std::function<void(core::Rgba8 color, bool add_to_history)> apply_color;
  // Transient status text.
  std::function<void(const QString &)> hud;
  // Saves the current document, returns false and reports via hud on failure.
  std::function<bool()> save_document;
  std::function<bool(const std::filesystem::path &)> save_document_as;
  std::function<bool(const std::filesystem::path &)> export_png;
  std::function<bool(const std::filesystem::path &)> import_image_as_layer;
  std::function<void()> close_to_gallery;
  // Reloads the preset database after an import.
  std::function<void()> reload_presets;
  // Switches the active tool and keeps the dock highlight in sync.
  std::function<void(Canvas::Tool)> set_tool;
  // Called after a structural document change (layer add/delete/undo/...).
  std::function<void()> document_structure_changed;
  // Shows another sheet by id ("color", "brushes", ...). Empty id closes.
  std::function<void(const QString &)> open_sheet;

  core::Rgba8 current_color() const {
    return canvas ? canvas->brush_settings().color : core::Rgba8{24, 24, 28, 255};
  }
};

} // namespace kreart::ui::mobile
