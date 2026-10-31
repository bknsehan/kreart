#pragma once

#include <QComboBox>
#include <QBasicTimer>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "kreart/core/document.h"
#include "kreart/core/layer.h"

namespace kreart::ui {

class Canvas;

// Helper function to render a 40x40 (or custom size) thumbnail with checkerboard background,
// downsampled image pixels, folder/clipping overlay, and 2px blue outline when active.
QPixmap render_layer_thumbnail(const core::Layer &layer, int size = 40, bool is_active = false);
QPixmap render_layer_preview(const core::Layer &layer, int max_w = 320, int max_h = 200);
QPixmap render_selection_preview(const core::Document &doc, const Canvas *canvas, int max_w = 360, int max_h = 240);

// Selection Layer Header Card shown above the layer tree
class SelectionLayerCard : public QFrame {
  Q_OBJECT
public:
  explicit SelectionLayerCard(QWidget *parent = nullptr);

  QString subtitle_text() const { return subtitle_label_ ? subtitle_label_->text() : QString(); }
  void update_status(bool active, int w = 0, int h = 0);
  void set_selected(bool sel);
  bool is_selected() const { return selected_; }
  QPushButton *preview_button() const { return preview_btn_; }
signals:
  void clicked();
  void previewClicked();

protected:
  void mousePressEvent(QMouseEvent *event) override;

private:
  QWidget *spacer_ = nullptr;
  QLabel *icon_label_ = nullptr;
  QLabel *title_label_ = nullptr;
  QLabel *subtitle_label_ = nullptr;
  QPushButton *preview_btn_ = nullptr;
  bool selected_ = false;
};

// Custom LayerTreeWidget with strict hierarchy invariants and direct Document drag-and-drop
class LayerTreeWidget : public QTreeWidget {
  Q_OBJECT
public:
  explicit LayerTreeWidget(core::Document &doc, Canvas *canvas, QWidget *parent = nullptr);

  core::Document &document() { return document_; }
  const core::Document &document() const { return document_; }
  void set_rebuild_callback(std::function<void()> cb) { rebuild_cb_ = std::move(cb); }
  bool perform_drop(std::size_t from_idx, std::size_t target_idx, double rel_y);
  void dropEvent(QDropEvent *event) override;

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
private:
  core::Document &document_;
  Canvas *canvas_ = nullptr;
  std::function<void()> rebuild_cb_;
};

// Rich custom row widget for each item in the LayerTreeWidget
class LayerRowWidget : public QWidget {
  Q_OBJECT
public:
  explicit LayerRowWidget(std::size_t layer_index, const core::Layer &layer, bool is_active,
                          int child_count, int depth = 0, LayerTreeWidget *tree = nullptr, QWidget *parent = nullptr);

  std::size_t layer_index() const { return layer_index_; }
  QPushButton *preview_button() const { return preview_btn_; }
  void update_state(const core::Layer &layer, bool is_active, int child_count);
  void set_selected(bool sel);

signals:
  void selected(std::size_t index, Qt::KeyboardModifiers mods);
  void visibilityToggled(std::size_t index);
  void expandToggled(std::size_t index);
  void lockToggled(std::size_t index);
  void renameRequested(std::size_t index);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void timerEvent(QTimerEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  QString build_subtitle(const core::Layer &layer, int child_count);
  void update_drag_autoscroll(const QPoint &global_pos);
  void stop_drag_autoscroll();
  std::size_t layer_index_;
  LayerTreeWidget *tree_widget_ = nullptr;
  QPushButton *chevron_btn_ = nullptr;
  QLabel *icon_label_ = nullptr;
  QPushButton *preview_btn_ = nullptr;
  QLabel *name_label_ = nullptr;
  QLabel *subtitle_label_ = nullptr;
  QPushButton *eye_btn_ = nullptr;
  QPushButton *lock_btn_ = nullptr;
  QPoint drag_start_pos_;
  int drop_indicator_pos_ = 0; // -1: above, 0: inside/none, 1: below
  bool drag_over_ = false;
  bool dragging_ = false;
  QBasicTimer scroll_timer_;
  int scroll_speed_ = 0; // pixels per scroll tick while dragging
};

// Unified LayerPanel matching GTK4 / Reference Mockup
class LayerPanel : public QWidget {
  Q_OBJECT
public:
  explicit LayerPanel(core::Document &doc, Canvas *canvas, QWidget *parent = nullptr);
  ~LayerPanel() override = default;

  LayerTreeWidget *tree_widget() const { return tree_widget_; }
  SelectionLayerCard *selection_card() const { return selection_card_; }
  QSlider *opacity_slider() const { return opacity_slider_; }
  QComboBox *blend_mode_combo() const { return blend_mode_combo_; }
  QComboBox *screentone_combo() const { return screentone_combo_; }
  QLineEdit *search_edit() const { return search_edit_; }

  void rebuild_layer_list();
  void update_controls();
  bool import_image(const std::filesystem::path &path);

  void clear_selection();
  void select_layer(std::size_t index, bool multi = false);
  const std::unordered_set<std::size_t> &selected_layers() const { return selected_layers_; }
signals:
  void activeLayerChanged(std::size_t index);
  void layerModified();

public slots:
  void on_add_layer();
  void on_add_group();
  void on_duplicate_layer();
  void on_merge_down();
  void on_delete_layer();
  void on_toggle_clipping_mask();
  void on_toggle_alpha_lock();
  void on_toggle_layer_lock();
  void on_rename_layer(std::size_t index = static_cast<std::size_t>(-1));
  void on_import_image_clicked();
  void on_flip_horizontal();
  void on_flip_vertical();
  void on_move_layer_up();
  void on_move_layer_down();
  void on_opacity_slider_changed(int val);
  void on_blend_mode_changed(int idx);
  void on_screentone_changed(int idx);

private:
  void setup_ui();
  void schedule_rebuild();
  void show_context_menu(std::size_t index, const QPoint &global_pos);
  void update_row_selection_highlights();
  void update_item_highlight_recursive(QTreeWidgetItem *item, std::size_t active_idx, bool has_act);
  void sync_selection_to_active();

  core::Document &document_;
  Canvas *canvas_ = nullptr;
  bool updating_ui_ = false;
  bool opacity_drag_active_ = false;
  bool opacity_dirty_ = false;
  QTimer rebuild_timer_;

  SelectionLayerCard *selection_card_ = nullptr;
  QLineEdit *search_edit_ = nullptr;
  LayerTreeWidget *tree_widget_ = nullptr;

  // Bottom controls
  std::unordered_set<std::size_t> selected_layers_;
  QPushButton *opacity_dec_btn_ = nullptr;
  QSlider *opacity_slider_ = nullptr;
  QPushButton *opacity_inc_btn_ = nullptr;
  QLabel *opacity_label_ = nullptr;

  QPushButton *clipping_btn_ = nullptr;
  QPushButton *alpha_lock_btn_ = nullptr;
  QComboBox *blend_mode_combo_ = nullptr;
  QComboBox *screentone_combo_ = nullptr;

  // Bottom action bar
  QPushButton *add_layer_btn_ = nullptr;
  QPushButton *add_group_btn_ = nullptr;
  QPushButton *import_btn_ = nullptr;
  QPushButton *duplicate_btn_ = nullptr;
  QPushButton *merge_down_btn_ = nullptr;
  QPushButton *delete_btn_ = nullptr;
  QPushButton *flip_h_btn_ = nullptr;
  QPushButton *flip_v_btn_ = nullptr;
  QPushButton *move_up_btn_ = nullptr;
  QPushButton *move_down_btn_ = nullptr;
};

} // namespace kreart::ui
