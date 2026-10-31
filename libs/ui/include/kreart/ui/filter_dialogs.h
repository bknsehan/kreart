#pragma once

#include <memory>
#include <string>

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>

#include "kreart/core/document.h"
#include "kreart/core/filters.h"
#include "kreart/ui/canvas.h"

namespace kreart::ui {

class BaseFilterDialog : public QDialog {
  Q_OBJECT

public:
  BaseFilterDialog(QWidget *parent, core::Document &document, Canvas &canvas,
                   const QString &title);
  ~BaseFilterDialog() override;

protected:
  void setup_dialog(QWidget *content_widget);
  virtual void apply_filter() = 0;
  virtual void reset_params() = 0;

  void update_preview();

  core::Document &document_;
  Canvas &canvas_;
  core::TiledImage original_image_;
  bool is_committed_ = false;
  bool updating_ = false;

  QCheckBox *preview_check_ = nullptr;
  QPushButton *reset_btn_ = nullptr;
  QPushButton *cancel_btn_ = nullptr;
  QPushButton *apply_btn_ = nullptr;
};

class HsvFilterDialog : public BaseFilterDialog {
  Q_OBJECT

public:
  HsvFilterDialog(QWidget *parent, core::Document &document, Canvas &canvas);

protected:
  void apply_filter() override;
  void reset_params() override;

private:
  QSlider *hue_slider_ = nullptr;
  QLabel *hue_label_ = nullptr;
  QSlider *sat_slider_ = nullptr;
  QLabel *sat_label_ = nullptr;
  QSlider *val_slider_ = nullptr;
  QLabel *val_label_ = nullptr;
};

class BrightnessContrastDialog : public BaseFilterDialog {
  Q_OBJECT

public:
  BrightnessContrastDialog(QWidget *parent, core::Document &document, Canvas &canvas);

protected:
  void apply_filter() override;
  void reset_params() override;

private:
  QSlider *brightness_slider_ = nullptr;
  QLabel *brightness_label_ = nullptr;
  QSlider *contrast_slider_ = nullptr;
  QLabel *contrast_label_ = nullptr;
};

class ColorBalanceDialog : public BaseFilterDialog {
  Q_OBJECT

public:
  ColorBalanceDialog(QWidget *parent, core::Document &document, Canvas &canvas);

protected:
  void apply_filter() override;
  void reset_params() override;

private:
  void sync_sliders();

  core::ColorBalanceParams params_;
  int active_tone_ = 1; // 0 = Shadows, 1 = Midtones, 2 = Highlights

  QComboBox *tone_combo_ = nullptr;
  QCheckBox *preserve_lum_check_ = nullptr;
  QSlider *cr_slider_ = nullptr;
  QLabel *cr_label_ = nullptr;
  QSlider *mg_slider_ = nullptr;
  QLabel *mg_label_ = nullptr;
  QSlider *yb_slider_ = nullptr;
  QLabel *yb_label_ = nullptr;
};

class GaussianBlurDialog : public BaseFilterDialog {
  Q_OBJECT

public:
  GaussianBlurDialog(QWidget *parent, core::Document &document, Canvas &canvas);

protected:
  void apply_filter() override;
  void reset_params() override;

private:
  QSlider *radius_slider_ = nullptr;
  QLabel *radius_label_ = nullptr;
};

} // namespace kreart::ui
