#include "kreart/ui/preferences_dialog.h"
#include "kreart/resources/resource_database.h"
#include "kreart/ui/icon_helper.h"

#include <QComboBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>
#include <cmath>

namespace kreart::ui {

struct InstalledBrushItem {
  std::filesystem::path path;
  std::string name;
  bool is_bundle = false;
  bool enabled = true;
};

static std::vector<InstalledBrushItem> scan_installed_brush_items() {
  std::vector<InstalledBrushItem> items;
  const auto user_dir = resources::ResourceDatabase::user_resource_directory();
  std::error_code ec;

  // 1. Installed Bundles
  const auto bundles_dir = user_dir / "bundles";
  if (std::filesystem::is_directory(bundles_dir, ec)) {
    for (const auto &entry : std::filesystem::directory_iterator(bundles_dir, ec)) {
      if (!entry.is_regular_file()) continue;
      const std::string filename = entry.path().filename().string();
      if (filename.ends_with(".bundle")) {
        items.push_back({entry.path(), entry.path().stem().string(), true, true});
      } else if (filename.ends_with(".bundle.disabled")) {
        std::string stem = filename.substr(0, filename.size() - 16);
        items.push_back({entry.path(), stem, true, false});
      }
    }
  }

  // 2. Installed Loose Presets (.kpp)
  const auto presets_dir = user_dir / "presets";
  if (std::filesystem::is_directory(presets_dir, ec)) {
    for (const auto &entry : std::filesystem::directory_iterator(presets_dir, ec)) {
      if (!entry.is_regular_file()) continue;
      const std::string filename = entry.path().filename().string();
      if (filename.ends_with(".kpp")) {
        items.push_back({entry.path(), entry.path().stem().string(), false, true});
      } else if (filename.ends_with(".kpp.disabled")) {
        std::string stem = filename.substr(0, filename.size() - 13);
        items.push_back({entry.path(), stem, false, false});
      }
    }
  }

  std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
    return a.name < b.name;
  });

  return items;
}

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Preferences"));
  setModal(true);
  setMinimumSize(660, 500);
  resize(720, 540);
  build_ui();
}

void PreferencesDialog::build_ui() {
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(14, 14, 14, 14);
  root_layout->setSpacing(10);

  // Main body: sidebar on left, stacked pages on right
  auto *body_layout = new QHBoxLayout();
  body_layout->setSpacing(12);

  // Left Sidebar
  nav_list_ = new QListWidget(this);
  nav_list_->setFixedWidth(170);
  nav_list_->setFocusPolicy(Qt::NoFocus);
  nav_list_->setIconSize(QSize(20, 20));
  nav_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const auto *acc_pal = AppSettings::accent_from_id(AppSettings::instance().accent().toStdString());
  const QString acc_col = acc_pal ? QString::fromUtf8(acc_pal->dark_accent) : QString("#2563eb");
  nav_list_->setStyleSheet(QString(
      "QListWidget {"
      "  background-color: rgba(255, 255, 255, 0.03);"
      "  border: 1px solid rgba(255, 255, 255, 0.08);"
      "  border-radius: 8px;"
      "  padding: 6px 4px;"
      "  outline: none;"
      "}"
      "QListWidget::item {"
      "  padding: 10px 12px;"
      "  border-radius: 6px;"
      "  margin: 2px 4px;"
      "  font-size: 13px;"
      "  font-weight: 500;"
      "  color: rgba(255, 255, 255, 0.85);"
      "}"
      "QListWidget::item:selected {"
      "  background-color: %1;"
      "  color: #ffffff;"
      "}"
      "QListWidget::item:hover:!selected {"
      "  background-color: rgba(255, 255, 255, 0.06);"
      "}").arg(acc_col));

  auto add_nav_item = [this](const QString &text, const QString &icon_path) {
    auto *item = new QListWidgetItem(text, nav_list_);
    if (!icon_path.isEmpty()) {
      item->setIcon(IconHelper::load_symbolic(icon_path, palette(), 20));
    }
  };

  add_nav_item(tr("General"), ":/icons/icons/folder-symbolic.svg");
  add_nav_item(tr("Appearance"), ":/icons/icons/emblem-system-symbolic.svg");
  add_nav_item(tr("Brushes"), ":/icons/icons/tool-brush-symbolic.svg");
  add_nav_item(tr("Tablet & Pen"), ":/icons/icons/tool-swap-symbolic.svg");

  body_layout->addWidget(nav_list_);

  // Right Stacked Pages
  pages_ = new QStackedWidget(this);
  pages_->setStyleSheet("QStackedWidget { background: transparent; }");

  // Page 0: General
  auto *page_general = new QWidget(pages_);
  auto *page_gen_layout = new QVBoxLayout(page_general);
  page_gen_layout->setContentsMargins(8, 4, 8, 8);
  page_gen_layout->setSpacing(12);

  auto *startup_group = new QGroupBox(tr("Startup Screen & Projects"), page_general);
  auto *startup_layout = new QVBoxLayout(startup_group);
  startup_layout->setContentsMargins(14, 16, 14, 14);
  startup_layout->setSpacing(10);

  startup_gallery_radio_ = new QRadioButton(tr("Open Project Gallery on startup (Default)"), startup_group);
  startup_blank_radio_ = new QRadioButton(tr("Start with Blank Canvas"), startup_group);

  if (AppSettings::instance().startup_mode() == StartupMode::BLANK_CANVAS) {
    startup_blank_radio_->setChecked(true);
  } else {
    startup_gallery_radio_->setChecked(true);
  }

  connect(startup_gallery_radio_, &QRadioButton::toggled, this, [](bool checked) {
    if (checked) AppSettings::instance().set_startup_mode(StartupMode::GALLERY);
  });
  connect(startup_blank_radio_, &QRadioButton::toggled, this, [](bool checked) {
    if (checked) AppSettings::instance().set_startup_mode(StartupMode::BLANK_CANVAS);
  });

  startup_layout->addWidget(startup_gallery_radio_);
  startup_layout->addWidget(startup_blank_radio_);

  auto *folder_row = new QHBoxLayout();
  auto *folder_path_lbl = new QLabel(AppSettings::instance().projects_directory(), startup_group);
  folder_path_lbl->setStyleSheet("color: gray; font-size: 11px;");
  folder_path_lbl->setWordWrap(true);

  auto *browse_folder_btn = new QPushButton(tr("Browse..."), startup_group);
  connect(browse_folder_btn, &QPushButton::clicked, this, [this, folder_path_lbl]() {
    const QString cur = AppSettings::instance().projects_directory();
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Select Projects Directory"), cur);
    if (!chosen.isEmpty() && chosen != cur) {
      AppSettings::instance().set_projects_directory(chosen);
      folder_path_lbl->setText(chosen);
    }
  });

  folder_row->addWidget(folder_path_lbl, 1);
  folder_row->addWidget(browse_folder_btn);
  startup_layout->addLayout(folder_row);

  page_gen_layout->addWidget(startup_group);
  page_gen_layout->addStretch(1);
  pages_->addWidget(page_general);

  // Page 1: Appearance
  auto *page_appearance = new QWidget(pages_);
  auto *page_app_layout = new QVBoxLayout(page_appearance);
  page_app_layout->setContentsMargins(8, 4, 8, 8);
  page_app_layout->setSpacing(12);

  auto *theme_group = new QGroupBox(tr("Appearance Theme"), page_appearance);
  auto *theme_layout = new QVBoxLayout(theme_group);
  theme_layout->setContentsMargins(14, 16, 14, 14);
  theme_layout->setSpacing(10);

  system_radio_ = new QRadioButton(tr("Follow Desktop Environment (Default)"), theme_group);
  dark_radio_ = new QRadioButton(tr("Dark Theme"), theme_group);
  light_radio_ = new QRadioButton(tr("Light Theme"), theme_group);

  theme_layout->addWidget(system_radio_);
  theme_layout->addWidget(dark_radio_);
  theme_layout->addWidget(light_radio_);



  const ThemeMode current_theme = AppSettings::instance().theme();
  updating_ui_ = true;
  if (current_theme == ThemeMode::DARK) dark_radio_->setChecked(true);
  else if (current_theme == ThemeMode::LIGHT) light_radio_->setChecked(true);
  else system_radio_->setChecked(true);
  updating_ui_ = false;

  connect(system_radio_, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked && !updating_ui_) on_theme_selected(ThemeMode::SYSTEM);
  });
  connect(dark_radio_, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked && !updating_ui_) on_theme_selected(ThemeMode::DARK);
  });
  connect(light_radio_, &QRadioButton::toggled, this, [this](bool checked) {
    if (checked && !updating_ui_) on_theme_selected(ThemeMode::LIGHT);
  });

  page_app_layout->addWidget(theme_group);

  // Transparency Background
  auto *trans_group = new QGroupBox(tr("Transparency Background"), page_appearance);
  auto *trans_layout = new QVBoxLayout(trans_group);
  trans_layout->setContentsMargins(14, 16, 14, 14);
  trans_layout->setSpacing(10);

  trans_follow_device_check_ = new QCheckBox(tr("Follow device / system theme (Default)"), trans_group);
  trans_follow_device_check_->setChecked(AppSettings::instance().transparency_follow_device());
  trans_layout->addWidget(trans_follow_device_check_);

  trans_options_container_ = new QWidget(trans_group);
  auto *opts_layout = new QVBoxLayout(trans_options_container_);
  opts_layout->setContentsMargins(16, 4, 0, 4);
  opts_layout->setSpacing(8);

  trans_light_radio_ = new QRadioButton(tr("Light checkerboard"), trans_options_container_);
  trans_dark_radio_ = new QRadioButton(tr("Dark checkerboard"), trans_options_container_);
  trans_solid_radio_ = new QRadioButton(tr("Solid color"), trans_options_container_);

  const auto cur_mode = AppSettings::instance().transparency_mode();
  if (cur_mode == TransparencyMode::LIGHT_CHECKERBOARD) trans_light_radio_->setChecked(true);
  else if (cur_mode == TransparencyMode::DARK_CHECKERBOARD) trans_dark_radio_->setChecked(true);
  else trans_solid_radio_->setChecked(true);

  opts_layout->addWidget(trans_light_radio_);
  opts_layout->addWidget(trans_dark_radio_);

  auto *solid_row = new QHBoxLayout();
  solid_row->setContentsMargins(0, 0, 0, 0);
  solid_row->setSpacing(8);
  solid_row->addWidget(trans_solid_radio_);

  trans_color_btn_ = new QPushButton(trans_options_container_);
  trans_color_btn_->setFixedSize(60, 22);
  auto update_color_btn = [this](const QColor &col) {
    trans_color_btn_->setStyleSheet(QString("background-color: %1; border: 1px solid rgba(255,255,255,0.3); border-radius: 4px;").arg(col.name()));
  };
  update_color_btn(AppSettings::instance().transparency_solid_color());

  connect(trans_color_btn_, &QPushButton::clicked, this, [this, update_color_btn]() {
    const QColor initial = AppSettings::instance().transparency_solid_color();
    const QColor chosen = QColorDialog::getColor(initial, this, tr("Select Transparency Solid Color"));
    if (chosen.isValid()) {
      AppSettings::instance().set_transparency_solid_color(chosen);
      update_color_btn(chosen);
    }
  });

  solid_row->addWidget(trans_color_btn_);
  solid_row->addStretch(1);
  opts_layout->addLayout(solid_row);

  trans_options_container_->setEnabled(!trans_follow_device_check_->isChecked());
  trans_layout->addWidget(trans_options_container_);

  connect(trans_follow_device_check_, &QCheckBox::toggled, this, [this](bool checked) {
    trans_options_container_->setEnabled(!checked);
    AppSettings::instance().set_transparency_follow_device(checked);
  });
  connect(trans_light_radio_, &QRadioButton::toggled, this, [](bool checked) {
    if (checked) AppSettings::instance().set_transparency_mode(TransparencyMode::LIGHT_CHECKERBOARD);
  });
  connect(trans_dark_radio_, &QRadioButton::toggled, this, [](bool checked) {
    if (checked) AppSettings::instance().set_transparency_mode(TransparencyMode::DARK_CHECKERBOARD);
  });
  connect(trans_solid_radio_, &QRadioButton::toggled, this, [](bool checked) {
    if (checked) AppSettings::instance().set_transparency_mode(TransparencyMode::SOLID_COLOR);
  });

  page_app_layout->addWidget(trans_group);
  page_app_layout->addStretch(1);
  pages_->addWidget(page_appearance);

  // Page 2: Brushes
  auto *page_brushes = new QWidget(pages_);
  auto *page_br_layout = new QVBoxLayout(page_brushes);
  page_br_layout->setContentsMargins(8, 4, 8, 8);
  page_br_layout->setSpacing(12);

  auto *brush_group = new QGroupBox(tr("Brush Resources & Detection"), page_brushes);
  auto *brush_layout = new QVBoxLayout(brush_group);
  brush_layout->setContentsMargins(14, 16, 14, 14);
  brush_layout->setSpacing(10);

  detect_krita_check_ = new QCheckBox(tr("Detect system & user Krita brushes"), brush_group);
  detect_krita_check_->setChecked(AppSettings::instance().detect_krita_brushes());
  connect(detect_krita_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (!updating_ui_) {
      AppSettings::instance().set_detect_krita_brushes(checked);
      emit installedBrushesChanged();
    }
  });
  brush_layout->addWidget(detect_krita_check_);

  auto *import_btn = new QPushButton(tr("Import Brush Pack (.bundle / .kpp)..."), brush_group);
  connect(import_btn, &QPushButton::clicked, this, &PreferencesDialog::on_import_brush_pack);
  brush_layout->addWidget(import_btn);

  // Installed brushes list with generous vertical height
  auto *scroll = new QScrollArea(brush_group);
  scroll->setWidgetResizable(true);
  scroll->setMinimumHeight(240);

  auto *scroll_content = new QWidget(scroll);
  installed_brushes_layout_ = new QVBoxLayout(scroll_content);
  installed_brushes_layout_->setContentsMargins(6, 6, 6, 6);
  installed_brushes_layout_->setSpacing(6);
  scroll->setWidget(scroll_content);
  brush_layout->addWidget(scroll, 1);

  page_br_layout->addWidget(brush_group, 1);
  pages_->addWidget(page_brushes);
  refresh_installed_brushes_list();

  // Page 3: Tablet
  auto *page_tablet = new QWidget(pages_);
  auto *page_tab_layout = new QVBoxLayout(page_tablet);
  page_tab_layout->setContentsMargins(8, 4, 8, 8);
  page_tab_layout->setSpacing(12);

  auto *tablet_group = new QGroupBox(tr("Tablet Pressure Response"), page_tablet);
  auto *tablet_layout = new QVBoxLayout(tablet_group);
  tablet_layout->setContentsMargins(14, 16, 14, 14);
  tablet_layout->setSpacing(10);

  auto *tablet_desc = new QLabel(tr("Drag the curve control point to adjust stylus pressure sensitivity. Higher curve gives a softer feel; lower curve requires firmer pressure."), tablet_group);
  tablet_desc->setWordWrap(true);
  tablet_desc->setStyleSheet("color: rgba(255, 255, 255, 0.6); font-size: 12px; margin-bottom: 4px;");
  tablet_layout->addWidget(tablet_desc);

  pressure_curve_ = new PressureCurve(tablet_group);
  pressure_curve_->setMinimumHeight(220);

  const QString saved_curve = AppSettings::instance().pressure_curve();
  bool ok = false;
  const double saved_gamma = saved_curve.toDouble(&ok);
  if (ok && saved_gamma > 0.0) {
    pressure_curve_->set_gamma(saved_gamma);
  }

  connect(pressure_curve_, &PressureCurve::curveChanged, this, [](double gamma) {
    if (std::abs(gamma - 1.0) < 0.001) {
      AppSettings::instance().set_pressure_curve("0.0,0.0;1.0,1.0");
    } else {
      AppSettings::instance().set_pressure_curve(QString::number(gamma, 'f', 3));
    }
  });

  tablet_layout->addWidget(pressure_curve_, 1);

  auto *reset_row = new QHBoxLayout();
  reset_row->addStretch(1);
  reset_pressure_btn_ = new QPushButton(tr("Reset"), tablet_group);
  connect(reset_pressure_btn_, &QPushButton::clicked, this, [this]() {
    if (pressure_curve_) {
      pressure_curve_->reset();
    }
  });
  reset_row->addWidget(reset_pressure_btn_);
  tablet_layout->addLayout(reset_row);

  page_tab_layout->addWidget(tablet_group, 1);
  pages_->addWidget(page_tablet);

  // Connect sidebar selection to pages
  connect(nav_list_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
  nav_list_->setCurrentRow(0);

  body_layout->addWidget(pages_, 1);
  root_layout->addLayout(body_layout, 1);

  // Bottom action bar
  auto *btn_bar = new QHBoxLayout();
  btn_bar->setContentsMargins(0, 4, 0, 0);
  btn_bar->addStretch(1);
  auto *close_btn = new QPushButton(tr("Close"), this);
  close_btn->setDefault(true);
  close_btn->setFixedSize(90, 32);
  connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
  btn_bar->addWidget(close_btn);
  root_layout->addLayout(btn_bar);
}

void PreferencesDialog::on_theme_selected(ThemeMode mode) {
  AppSettings::instance().set_theme(mode);
}


void PreferencesDialog::on_import_brush_pack() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Import Brush Pack"), QString(),
      tr("Brush Bundles and Presets (*.bundle *.kpp)"));
  if (filename.isEmpty()) return;

  const std::filesystem::path src(filename.toStdString());
  const auto user_dir = resources::ResourceDatabase::user_resource_directory();
  std::error_code ec;

  if (src.extension() == ".bundle") {
    const auto dest_dir = user_dir / "bundles";
    std::filesystem::create_directories(dest_dir, ec);
    std::filesystem::copy_file(src, dest_dir / src.filename(),
                               std::filesystem::copy_options::overwrite_existing, ec);
  } else if (src.extension() == ".kpp") {
    const auto dest_dir = user_dir / "presets";
    std::filesystem::create_directories(dest_dir, ec);
    std::filesystem::copy_file(src, dest_dir / src.filename(),
                               std::filesystem::copy_options::overwrite_existing, ec);
  }

  refresh_installed_brushes_list();
  emit installedBrushesChanged();
}

void PreferencesDialog::refresh_installed_brushes_list() {
  if (!installed_brushes_layout_) return;

  QLayoutItem *child;
  while ((child = installed_brushes_layout_->takeAt(0)) != nullptr) {
    delete child->widget();
    delete child;
  }

  const auto items = scan_installed_brush_items();
  if (items.empty()) {
    auto *empty_lbl = new QLabel(tr("No custom brushes installed yet."), this);
    empty_lbl->setStyleSheet("color: gray;");
    installed_brushes_layout_->addWidget(empty_lbl);
    return;
  }

  for (const auto &item : items) {
    auto *row = new QWidget(this);
    auto *r_layout = new QHBoxLayout(row);
    r_layout->setContentsMargins(2, 2, 2, 2);

    auto *chk = new QCheckBox(QString::fromStdString(item.name) + (item.is_bundle ? " (bundle)" : " (preset)"), row);
    chk->setChecked(item.enabled);
    connect(chk, &QCheckBox::toggled, this, [this, item](bool checked) {
      std::error_code ec;
      if (checked) {
        if (item.path.string().ends_with(".disabled")) {
          std::string orig = item.path.string().substr(0, item.path.string().size() - 9);
          std::filesystem::rename(item.path, orig, ec);
        }
      } else {
        if (!item.path.string().ends_with(".disabled")) {
          std::filesystem::rename(item.path, item.path.string() + ".disabled", ec);
        }
      }
      emit installedBrushesChanged();
    });
    r_layout->addWidget(chk);

    r_layout->addStretch(1);

    auto *del_btn = new QPushButton(tr("Remove"), row);
    connect(del_btn, &QPushButton::clicked, this, [this, item]() {
      std::error_code ec;
      std::filesystem::remove(item.path, ec);
      refresh_installed_brushes_list();
      emit installedBrushesChanged();
    });
    r_layout->addWidget(del_btn);

    installed_brushes_layout_->addWidget(row);
  }
}

} // namespace kreart::ui
