#pragma once

#include <QDialog>
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QListWidget>
#include <QStackedWidget>
#include <filesystem>
#include <vector>

#include "kreart/ui/app_settings.h"
#include "kreart/ui/pressure_curve.h"

namespace kreart::ui {

class PreferencesDialog : public QDialog {
  Q_OBJECT

public:
  explicit PreferencesDialog(QWidget *parent = nullptr);
  ~PreferencesDialog() override = default;

  PressureCurve *pressure_curve() const { return pressure_curve_; }
  QPushButton *reset_pressure_button() const { return reset_pressure_btn_; }

  QRadioButton *startup_gallery_radio() const { return startup_gallery_radio_; }
  QRadioButton *startup_blank_radio() const { return startup_blank_radio_; }
  QCheckBox *transparency_follow_device_check() const { return trans_follow_device_check_; }
  QRadioButton *transparency_light_radio() const { return trans_light_radio_; }
  QRadioButton *transparency_dark_radio() const { return trans_dark_radio_; }
  QRadioButton *transparency_solid_radio() const { return trans_solid_radio_; }
  QPushButton *transparency_color_button() const { return trans_color_btn_; }
  QListWidget *navigation_list() const { return nav_list_; }
  QStackedWidget *pages() const { return pages_; }
signals:
  void installedBrushesChanged();

private:
  void build_ui();
  void on_theme_selected(ThemeMode mode);
  void refresh_installed_brushes_list();
  void on_import_brush_pack();

  QListWidget *nav_list_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  QRadioButton *system_radio_ = nullptr;
  QRadioButton *dark_radio_ = nullptr;
  QRadioButton *light_radio_ = nullptr;
  QRadioButton *startup_gallery_radio_ = nullptr;
  QRadioButton *startup_blank_radio_ = nullptr;
  QCheckBox *detect_krita_check_ = nullptr;
  QVBoxLayout *installed_brushes_layout_ = nullptr;
  PressureCurve *pressure_curve_ = nullptr;
  QPushButton *reset_pressure_btn_ = nullptr;
  QCheckBox *trans_follow_device_check_ = nullptr;
  QWidget *trans_options_container_ = nullptr;
  QRadioButton *trans_light_radio_ = nullptr;
  QRadioButton *trans_dark_radio_ = nullptr;
  QRadioButton *trans_solid_radio_ = nullptr;
  QPushButton *trans_color_btn_ = nullptr;
  bool updating_ui_ = false;
};

} // namespace kreart::ui
