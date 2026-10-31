#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QColor>

#include "kreart/ui/canvas.h"

namespace kreart::ui {

class GridSetupDialog : public QDialog {
  Q_OBJECT

public:
  explicit GridSetupDialog(QWidget *parent, Canvas *canvas);
  ~GridSetupDialog() override = default;

private:
  void build_ui();
  void update_preview();
  void update_color_button();

  Canvas *canvas_ = nullptr;
  QCheckBox *show_grid_check_ = nullptr;
  QSpinBox *size_spin_ = nullptr;
  QSlider *size_slider_ = nullptr;
  QSlider *opacity_slider_ = nullptr;
  QPushButton *color_btn_ = nullptr;

  bool initial_show_ = false;
  int initial_size_ = 32;
  core::Rgba8 initial_color_{128, 128, 140, 90};

  QColor current_color_{128, 128, 140, 90};
};

} // namespace kreart::ui
