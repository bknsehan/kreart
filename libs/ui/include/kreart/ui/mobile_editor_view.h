#pragma once

#include <array>
#include <filesystem>
#include <memory>

#include <QWidget>

#include "kreart/core/document.h"
#include "kreart/resources/resource_database.h"
#include "kreart/ui/canvas.h"
#include "kreart/ui/color_history.h"
#include "kreart/ui/mobile/editor_context.h"
#include "kreart/ui/stroke_preview_cache.h"

class QLabel;
class QPushButton;
class QTimer;

namespace kreart::ui {

class MobileCanvas;

namespace mobile {
class EditorSheets;
class MobileHud;
class MobileSheet;
class PillButton;
class QuickWheel;
} // namespace mobile

// The touch-first Android editor. Owns the document, the MobileCanvas, the
// floating chrome (top capsule, bottom dock, brush scrubber) and the sheets.
class MobileEditorView : public QWidget {
  Q_OBJECT

public:
  explicit MobileEditorView(QWidget *parent = nullptr);
  ~MobileEditorView() override;

  void load_document(const std::filesystem::path &path);
  void create_new_document(int width, int height, const QString &title);
  void save_current_document();
  std::filesystem::path current_path() const { return current_path_; }

  Canvas *canvas();

  // Back button handling: closes the topmost overlay, returns true if consumed.
  bool close_topmost_overlay();
  // Opens a named sheet ("hub", "color", "brushes", "layers", ...).
  void open_sheet(const QString &id) { show_sheet(id); }
  // Saves and emits backToGalleryRequested().
  void request_back_to_gallery();

signals:
  void backToGalleryRequested();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  void build_ui();
  void layout_overlays();
  void update_title_only();
  void update_dock_highlights();
  void refresh_theme_icons();

  // Chrome
  void setup_top_capsule();
  void setup_bottom_dock();
  void setup_scrubber();
  void toggle_fullscreen_chrome();

  // Tools
  void set_active_tool(Canvas::Tool tool);
  void assign_dock_slot(int slot, Canvas::Tool tool);
  void open_tool_chooser(int target_slot);
  std::array<Canvas::Tool, 3> load_dock_slots() const;
  void save_dock_slots();

  // Document ops
  bool save_to(const std::filesystem::path &path);
  bool export_png(const std::filesystem::path &path);
  bool import_image_as_layer(const std::filesystem::path &path);
  void assign_layer_ids();
  void mark_clean();

  // Autosave
  void setup_autosave();
  void check_recovery();
  void reload_presets();

  void apply_brush_color(core::Rgba8 color, bool add_to_history);
  void show_sheet(const QString &id);

  core::Document document_;
  std::filesystem::path current_path_;
  QString project_title_ = QStringLiteral("Untitled");

  mobile::EditorContext ctx_;
  std::unique_ptr<mobile::EditorSheets> sheets_;

  resources::ResourceDatabase preset_database_;
  ColorHistory color_history_;
  StrokePreviewCache stroke_preview_cache_;

  MobileCanvas *canvas_ = nullptr;
  mobile::QuickWheel *quick_wheel_ = nullptr;
  mobile::MobileHud *hud_ = nullptr;

  // Top capsule
  QWidget *capsule_ = nullptr;
  QPushButton *back_btn_ = nullptr;
  QLabel *title_label_ = nullptr;
  QLabel *subtitle_label_ = nullptr;
  QPushButton *undo_btn_ = nullptr;
  QPushButton *redo_btn_ = nullptr;
  QPushButton *overflow_btn_ = nullptr;
  QWidget *peek_pill_ = nullptr;

  // Bottom dock
  QWidget *dock_ = nullptr;
  std::array<QPushButton *, 3> slot_buttons_{nullptr, nullptr, nullptr};
  QPushButton *color_chip_ = nullptr;
  QPushButton *studio_btn_ = nullptr;
  std::array<Canvas::Tool, 3> dock_slots_{Canvas::Tool::BRUSH, Canvas::Tool::ERASER,
                                          Canvas::Tool::LASSO_SELECT};

  // Brush scrubber
  QWidget *scrubber_ = nullptr;
  QLabel *scrubber_label_ = nullptr;
  bool scrubbing_ = false;
  int scrub_start_x_ = 0;
  double scrub_start_size_ = 32.0;

  // Tool chooser tray
  QWidget *tool_tray_ = nullptr;
  int tray_target_slot_ = 0;

  bool fullscreen_canvas_ = false;
  bool saving_ = false;
  QTimer *autosave_timer_ = nullptr;
  QTimer *dock_long_press_timer_ = nullptr;
  int dock_press_slot_ = -1;
};

} // namespace kreart::ui
