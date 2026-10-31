#pragma once

#include <functional>

#include <QWidget>

#include "kreart/ui/mobile/editor_context.h"
#include "kreart/ui/mobile/mobile_sheet.h"

namespace kreart::ui::mobile {

class EditorSheets;

// Each builder fills a freshly created MobileSheet with its body. They live in
// separate translation units to keep the sheet code readable.
void build_hub_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_color_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_brush_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_layer_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_tool_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_selection_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_image_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_filter_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_view_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_document_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);
void build_settings_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets);

// Called when the layer sheet becomes visible so it can rebuild rows cheaply
// (and only then).
void notify_layer_sheet_opened(MobileSheet *sheet);

} // namespace kreart::ui::mobile
