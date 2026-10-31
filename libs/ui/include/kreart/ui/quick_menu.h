#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>

#include "kreart/core/tiled_image.h"

namespace kreart::ui {

class QuickMenu : public QWidget {
  Q_OBJECT

public:
  struct PresetItem {
    std::size_t db_index = 0;
    std::string display_name;
    std::filesystem::path thumbnail_path;
    QPixmap preview_pixmap;

    PresetItem() = default;
    PresetItem(std::size_t idx, std::string name, std::filesystem::path path, QPixmap pix = {})
        : db_index(idx), display_name(std::move(name)), thumbnail_path(std::move(path)),
          preview_pixmap(std::move(pix)) {}
  };

  explicit QuickMenu(QWidget *parent = nullptr);
  ~QuickMenu() override = default;

  void popup_at(const QPoint &global_pos);

  void set_brush_color(core::Rgba8 color);
  void set_brush_size(double size);
  void set_brush_opacity(double opacity);
  void set_is_eraser(bool is_eraser);
  void set_recent_colors(const std::vector<core::Rgba8> &colors);
  void set_presets(const std::vector<PresetItem> &presets);
  void set_active_preset(std::size_t db_index);

signals:
  void colorSelected(const core::Rgba8 &color);
  void sizeChanged(double size);
  void opacityChanged(double opacity);
  void presetSelected(std::size_t db_index);
  void undoRequested();
  void redoRequested();
  void toggleEraserRequested();
  void zoomFitRequested();
  void resetRotationRequested();
  void mirrorCanvasRequested();

protected:
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void build_ui();
  void rebuild_swatches();
  void rebuild_presets();

  core::Rgba8 current_color_{24, 24, 28, 255};
  double current_size_ = 24.0;
  double current_opacity_ = 1.0;
  bool is_eraser_ = false;
  std::optional<std::size_t> active_preset_index_;

  std::vector<core::Rgba8> recent_colors_;
  std::vector<PresetItem> presets_;

  QPushButton *eraser_btn_ = nullptr;
  QSlider *size_slider_ = nullptr;
  QLabel *size_label_ = nullptr;
  QSlider *opacity_slider_ = nullptr;
  QLabel *opacity_label_ = nullptr;

  QHBoxLayout *swatch_layout_ = nullptr;
  QGridLayout *preset_grid_ = nullptr;
  std::vector<QPushButton *> swatch_buttons_;
  std::vector<QPushButton *> preset_buttons_;
  bool updating_controls_ = false;
};

} // namespace kreart::ui
