#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QMainWindow>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QTabWidget>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QTreeWidget>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>

namespace kreart::ui {
class ProjectGalleryWindow;
}

#include "kreart/core/document.h"
#include "kreart/resources/resource_database.h"
#include "kreart/ui/canvas.h"
#include "kreart/ui/color_history.h"
#include "kreart/ui/color_picker.h"
#include "kreart/ui/layer_panel.h"
#include "kreart/ui/quick_menu.h"
#include "kreart/ui/stroke_preview_cache.h"
#include "kreart/ui/filter_dialogs.h"
#include "kreart/ui/geometry_dialogs.h"
#include "kreart/ui/preferences_dialog.h"

namespace kreart::ui {

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  void open_file(const std::filesystem::path &path);
  bool import_image_as_layer(const std::filesystem::path &path);
  bool import_brush_pack(const std::filesystem::path &path);
  void select_preset(std::size_t db_idx);
  void create_new_document(int width, int height, const QString &title);
  void show_initial_gallery();
  void on_show_gallery();
  void reset_layout();
  void on_configure_shortcuts();
  void on_grid_settings();
  void toggle_preset_view_mode();
  void popup_quick_menu(double x = -1.0, double y = -1.0);
  QuickMenu *quick_menu() { return quick_menu_; }
  Canvas *canvas() { return canvas_; }
  QListWidget *preset_list() { return preset_list_; }
  std::optional<std::size_t> active_preset_index() const { return active_preset_index_; }
  const ColorHistory &color_history() const { return color_history_; }
  const std::vector<QPushButton *> &profile_swatch_buttons() const { return profile_swatch_buttons_; }
  const std::vector<QPushButton *> &recent_swatch_buttons() const { return recent_swatch_buttons_; }
  StudioColorPicker *studio_picker() { return studio_picker_; }
  LayerPanel *layer_panel() { return layer_panel_; }
  QTreeWidget *layer_tree();
  core::Document &document() { return document_; }
  const core::Document &document() const { return document_; }
  void rebuild_layer_list();
  void on_toggle_clipping_mask();
protected:
  void changeEvent(QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;
private:
  void build_ui();
  void setup_actions();
  void setup_menus();
  void setup_tool_rail();
  void setup_studio_dock();
  void setup_status_bar();
  void setup_quick_menu();
  void load_presets();
  void refresh_theme_icons();
  void apply_pressure_curve_from_settings();
  void rebuild_recent_menu();
  void apply_shortcuts();
  void on_shortcut_action(const QString &id);

  void update_title();
  void update_actions();
  void set_status(const QString &text);

  // Document actions
  void on_new_document();
  void on_open_document();
  void on_save_document();
  void on_save_as_document();
  void on_export_png();
  void on_import_image_as_layer();
  void on_import_brush_pack();
  void on_recover_damaged_project();
  void on_replay_timelapse();

  // Edit / Selection / Clipboard actions
  void on_undo();
  void on_redo();
  void on_cut();
  void on_copy();
  void on_paste();
  void on_paste_as_new_layer();
  void on_deselect();
  void on_invert_selection();
  void on_select_all();
  void on_clear_selection();
  void on_feather_selection();
  void on_grow_selection();
  void on_shrink_selection();
  void on_border_selection();
  // View actions
  void on_zoom_in();
  void on_zoom_out();
  void on_zoom_fit();
  void on_rotate_left();
  void on_rotate_right();
  void on_reset_rotation();
  void on_mirror_horizontal();
  void on_mirror_vertical();
  void on_pixelated_toggled(bool checked);
  void on_grid_toggled(bool checked);

  // Geometry / Filter actions
  void on_canvas_size();
  void on_scale_image();
  void on_filter_hsv();
  void on_filter_brightness_contrast();
  void on_filter_color_balance();
  void on_filter_gaussian_blur();
  void on_preferences();

  // Layer dock
  void on_add_layer();
  void on_add_group();
  void on_remove_layer();
  void on_merge_down();
  void on_rename_layer();
  void on_layer_opacity_changed(int value);
  void on_layer_blend_changed(int index);
  void on_layer_screentone_changed(int index);

  // Tool selection & dynamics
  void set_tool(Canvas::Tool tool);
  void on_brush_size_changed();
  void on_brush_opacity_changed();
  void on_brush_smoothing_changed();
  void on_smoothing_mode_changed(int index);
  void on_color_picked(const core::Rgba8 &color);
  void apply_brush_color(core::Rgba8 color, bool add_to_history = true);

  // Presets dock
  void rebuild_preset_dock();
  void on_preset_search_changed(const QString &text);
  void on_preset_category_changed(int index);

  // Autosave
  void setup_autosave();
  void trigger_autosave();

  core::Document document_;
  std::filesystem::path current_path_;
  resources::ResourceDatabase preset_database_;
  ColorHistory color_history_;
  StrokePreviewCache stroke_preview_cache_;

  Canvas *canvas_ = nullptr;
  ProjectGalleryWindow *gallery_window_ = nullptr;
  QuickMenu *quick_menu_ = nullptr;
  QDockWidget *studio_dock_ = nullptr;
  QTabWidget *studio_tabs_ = nullptr;

  // Status Bar readouts
  QLabel *cursor_label_ = nullptr;
  QLabel *pressure_label_ = nullptr;
  QLabel *tilt_label_ = nullptr;
  QLabel *zoom_label_ = nullptr;
  QLabel *status_label_ = nullptr;

  // Left Tool Rail
  QToolBar *tool_rail_ = nullptr;
  QActionGroup *tool_group_ = nullptr;
  QAction *brush_act_ = nullptr;
  QAction *eraser_act_ = nullptr;
  QAction *smudge_act_ = nullptr;
  QAction *rect_act_ = nullptr;
  QAction *ellipse_act_ = nullptr;
  QAction *lasso_act_ = nullptr;
  QAction *wand_act_ = nullptr;
  QAction *move_act_ = nullptr;
  QAction *picker_act_ = nullptr;
  QAction *fill_act_ = nullptr;
  QAction *swap_act_ = nullptr;
  QAction *sym_v_act_ = nullptr;
  QAction *sym_h_act_ = nullptr;
  QAction *reset_layout_act_ = nullptr;

  // Actions for menus
  QAction *undo_act_ = nullptr;
  QAction *redo_act_ = nullptr;
  QAction *save_act_ = nullptr;
  QAction *pixelated_act_ = nullptr;
  QAction *grid_act_ = nullptr;
  QAction *clip_mask_act_ = nullptr;
  void sync_system_clipboard_to_doc();
  bool system_clipboard_newer_ = false;
  // Preset Dock widgets
  enum class PresetViewMode { THUMBNAILS, DETAILS };
  PresetViewMode preset_view_mode_ = PresetViewMode::THUMBNAILS;
  QComboBox *preset_category_combo_ = nullptr;
  QLineEdit *preset_search_edit_ = nullptr;
  QPushButton *preset_view_toggle_btn_ = nullptr;
  QListWidget *preset_list_ = nullptr;
  std::optional<std::size_t> active_preset_index_;

  // Color Studio widgets
  QPushButton *color_well_btn_ = nullptr;
  StudioColorPicker *studio_picker_ = nullptr;
  std::vector<QPushButton *> recent_swatch_buttons_;
  std::vector<QPushButton *> profile_swatch_buttons_;
  QComboBox *profile_combo_ = nullptr;

  // Brush Dynamics widgets
  QSlider *size_slider_ = nullptr;
  QSpinBox *size_spin_ = nullptr;
  QSlider *opacity_slider_ = nullptr;
  QSlider *smoothing_slider_ = nullptr;
  QComboBox *smoothing_mode_combo_ = nullptr;
  QCheckBox *tilt_check_ = nullptr;
  QCheckBox *texture_check_ = nullptr;
  QComboBox *texture_pattern_combo_ = nullptr;
  QSlider *texture_scale_slider_ = nullptr;
  QSlider *texture_strength_slider_ = nullptr;
  QWidget *smudge_controls_widget_ = nullptr;
  QSlider *color_rate_slider_ = nullptr;
  QSlider *smudge_rate_slider_ = nullptr;
  QWidget *wand_controls_widget_ = nullptr;
  QSlider *wand_tolerance_slider_ = nullptr;
  QCheckBox *wand_contiguous_check_ = nullptr;

  // Layer Dock widgets
  LayerPanel *layer_panel_ = nullptr;
  QTreeWidget *layer_tree_ = nullptr;
  QMenu *recent_menu_ = nullptr;
  std::unordered_map<std::string, QAction *> action_by_id_;

  // Autosave timer
  QTimer autosave_timer_;
  bool updating_ui_ = false;
};

} // namespace kreart::ui
