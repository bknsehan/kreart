#include "kreart/ui/shortcuts.h"
#include <QCoreApplication>
#include <QKeySequence>
#include <QString>
#include <cstring>

namespace kreart::ui {

namespace {

const std::vector<ShortcutDef> &table() {
  static const std::vector<ShortcutDef> defs = {
      // Tools
      {"tool.brush", QT_TRANSLATE_NOOP("Shortcut", "Brush Tool"), QKeySequence(Qt::Key_B)},
      {"tool.eraser", QT_TRANSLATE_NOOP("Shortcut", "Eraser Tool"), QKeySequence("Shift+E")},
      {"tool.smudge", QT_TRANSLATE_NOOP("Shortcut", "Smudge Tool"), QKeySequence(Qt::Key_S)},
      {"tool.rect_select", QT_TRANSLATE_NOOP("Shortcut", "Rect Select"), QKeySequence(Qt::Key_R)},
      {"tool.ellipse_select", QT_TRANSLATE_NOOP("Shortcut", "Ellipse Select"), QKeySequence(Qt::Key_E)},
      {"tool.lasso", QT_TRANSLATE_NOOP("Shortcut", "Lasso Select"), QKeySequence(Qt::Key_L)},
      {"tool.wand", QT_TRANSLATE_NOOP("Shortcut", "Magic Wand"), QKeySequence(Qt::Key_W)},
      {"tool.move", QT_TRANSLATE_NOOP("Shortcut", "Move Tool"), QKeySequence(Qt::Key_T)},
      {"tool.picker", QT_TRANSLATE_NOOP("Shortcut", "Color Picker"), QKeySequence(Qt::Key_P)},
      {"tool.fill", QT_TRANSLATE_NOOP("Shortcut", "Fill Tool"), QKeySequence(Qt::Key_F)},
      {"tool.swap_brush_eraser", QT_TRANSLATE_NOOP("Shortcut", "Swap Brush/Eraser"), QKeySequence(Qt::Key_X)},
      // Edit
      {"edit.undo", QT_TRANSLATE_NOOP("Shortcut", "Undo"), QKeySequence::Undo},
      {"edit.redo", QT_TRANSLATE_NOOP("Shortcut", "Redo"), QKeySequence::Redo},
      {"edit.quick_menu", QT_TRANSLATE_NOOP("Shortcut", "Popup Quick Menu"), QKeySequence(Qt::Key_Q)},
      {"edit.commit_selection", QT_TRANSLATE_NOOP("Shortcut", "Commit Selection"), QKeySequence(Qt::Key_Return)},
      {"edit.cancel_selection", QT_TRANSLATE_NOOP("Shortcut", "Cancel/Clear Selection"), QKeySequence(Qt::Key_Escape)},
      // View — rotate / zoom
      {"view.rotate_left", QT_TRANSLATE_NOOP("Shortcut", "Rotate Canvas Left"), QKeySequence(Qt::Key_4)},
      {"view.rotate_right", QT_TRANSLATE_NOOP("Shortcut", "Rotate Canvas Right"), QKeySequence(Qt::Key_6)},
      {"view.reset_rotation", QT_TRANSLATE_NOOP("Shortcut", "Reset Canvas Rotation"), QKeySequence(Qt::Key_5)},
      {"view.mirror_horizontal", QT_TRANSLATE_NOOP("Shortcut", "Mirror Horizontal"), QKeySequence(Qt::Key_M)},
      {"view.zoom_in", QT_TRANSLATE_NOOP("Shortcut", "Zoom In"), QKeySequence("Ctrl++")},
      {"view.zoom_out", QT_TRANSLATE_NOOP("Shortcut", "Zoom Out"), QKeySequence("Ctrl+-")},
      {"view.zoom_fit", QT_TRANSLATE_NOOP("Shortcut", "Zoom to Fit"), QKeySequence("Ctrl+0")},
      // Brush size (no selection)
      {"brush.bigger", QT_TRANSLATE_NOOP("Shortcut", "Bigger Brush / Rotate Selection Right"), QKeySequence(Qt::Key_BracketRight)},
      {"brush.smaller", QT_TRANSLATE_NOOP("Shortcut", "Smaller Brush / Rotate Selection Left"), QKeySequence(Qt::Key_BracketLeft)},
      // Selection transform (when a selection is active)
      {"selection.rotate_left", QT_TRANSLATE_NOOP("Shortcut", "Rotate Selection Left"), QKeySequence(Qt::Key_BracketLeft)},
      {"selection.rotate_right", QT_TRANSLATE_NOOP("Shortcut", "Rotate Selection Right"), QKeySequence(Qt::Key_BracketRight)},
      {"selection.rotate_left_big", QT_TRANSLATE_NOOP("Shortcut", "Rotate Selection Left 45°"), QKeySequence(Qt::Key_BraceLeft)},
      {"selection.rotate_right_big", QT_TRANSLATE_NOOP("Shortcut", "Rotate Selection Right 45°"), QKeySequence(Qt::Key_BraceRight)},
      {"selection.scale_down", QT_TRANSLATE_NOOP("Shortcut", "Scale Selection Down"), QKeySequence(Qt::Key_Minus)},
      {"selection.scale_up", QT_TRANSLATE_NOOP("Shortcut", "Scale Selection Up"), QKeySequence(Qt::Key_Plus)},
      {"selection.clear_content", QT_TRANSLATE_NOOP("Shortcut", "Clear Selection Content"), QKeySequence(QKeySequence::Delete)},
  };
  return defs;
}

} // namespace

const std::vector<ShortcutDef> &default_shortcuts() {
  return table();
}

QString shortcut_label(const char *id) {
  for (const auto &d : table()) {
    if (std::strcmp(d.id, id) == 0) {
      return QCoreApplication::translate("Shortcut", d.label);
    }
  }
  return {};
}

} // namespace kreart::ui
