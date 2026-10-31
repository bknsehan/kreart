#include "kreart/ui/grid_dialog.h"
#include "kreart/ui/app_settings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QColorDialog>
#include <QDialogButtonBox>

namespace kreart::ui {

GridSetupDialog::GridSetupDialog(QWidget *parent, Canvas *canvas)
    : QDialog(parent), canvas_(canvas) {
  setWindowTitle(tr("Grid & Guidelines Setup"));
  setModal(true);
  resize(420, 380);

  if (canvas_) {
    initial_show_ = canvas_->show_grid();
    initial_size_ = canvas_->grid_size();
    initial_color_ = canvas_->grid_color();
    current_color_ = QColor(initial_color_.r, initial_color_.g, initial_color_.b, initial_color_.a);
  }

  build_ui();
}

void GridSetupDialog::build_ui() {
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(18, 18, 18, 18);
  root_layout->setSpacing(14);

  // 1. General Grid Settings
  auto *gen_group = new QGroupBox(tr("Grid Display"), this);
  auto *gen_layout = new QVBoxLayout(gen_group);

  show_grid_check_ = new QCheckBox(tr("Show Grid on Canvas"), gen_group);
  show_grid_check_->setChecked(initial_show_);
  connect(show_grid_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_) canvas_->set_show_grid(checked);
  });
  gen_layout->addWidget(show_grid_check_);
  root_layout->addWidget(gen_group);

  // 2. Grid Sizing & Spacing
  auto *size_group = new QGroupBox(tr("Cell Sizing"), this);
  auto *size_layout = new QVBoxLayout(size_group);

  auto *size_row = new QHBoxLayout();
  size_row->addWidget(new QLabel(tr("Spacing (px):"), size_group));

  size_spin_ = new QSpinBox(size_group);
  size_spin_->setRange(4, 512);
  size_spin_->setSingleStep(4);
  size_spin_->setValue(initial_size_);
  size_row->addWidget(size_spin_);

  size_slider_ = new QSlider(Qt::Horizontal, size_group);
  size_slider_->setRange(4, 256);
  size_slider_->setValue(std::clamp(initial_size_, 4, 256));
  size_row->addWidget(size_slider_, 1);

  connect(size_spin_, &QSpinBox::valueChanged, this, [this](int val) {
    size_slider_->blockSignals(true);
    size_slider_->setValue(std::clamp(val, 4, 256));
    size_slider_->blockSignals(false);
    update_preview();
  });

  connect(size_slider_, &QSlider::valueChanged, this, [this](int val) {
    size_spin_->blockSignals(true);
    size_spin_->setValue(val);
    size_spin_->blockSignals(false);
    update_preview();
  });

  size_layout->addLayout(size_row);
  root_layout->addWidget(size_group);

  // 3. Grid Color & Opacity
  auto *color_group = new QGroupBox(tr("Line Color & Opacity"), this);
  auto *color_layout = new QVBoxLayout(color_group);

  auto *color_row = new QHBoxLayout();
  color_row->addWidget(new QLabel(tr("Color:"), color_group));

  color_btn_ = new QPushButton(color_group);
  color_btn_->setFixedHeight(28);
  update_color_button();
  connect(color_btn_, &QPushButton::clicked, this, [this]() {
    const QColor picked = QColorDialog::getColor(current_color_, this, tr("Select Grid Color"),
                                                 QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
      current_color_ = picked;
      opacity_slider_->setValue(static_cast<int>(std::round(picked.alphaF() * 100.0)));
      update_color_button();
      update_preview();
    }
  });
  color_row->addWidget(color_btn_, 1);
  color_layout->addLayout(color_row);

  // Opacity Slider
  auto *op_row = new QHBoxLayout();
  op_row->addWidget(new QLabel(tr("Opacity:"), color_group));

  opacity_slider_ = new QSlider(Qt::Horizontal, color_group);
  opacity_slider_->setRange(1, 100);
  opacity_slider_->setValue(static_cast<int>(std::round((current_color_.alpha() / 255.0) * 100.0)));
  auto *op_val_label = new QLabel(QString("%1%").arg(opacity_slider_->value()), color_group);
  op_val_label->setFixedWidth(38);

  connect(opacity_slider_, &QSlider::valueChanged, this, [this, op_val_label](int val) {
    op_val_label->setText(QString("%1%").arg(val));
    current_color_.setAlphaF(val / 100.0);
    update_color_button();
    update_preview();
  });

  op_row->addWidget(opacity_slider_, 1);
  op_row->addWidget(op_val_label);
  color_layout->addLayout(op_row);

  // Quick Preset Colors
  auto *preset_row = new QHBoxLayout();
  preset_row->addWidget(new QLabel(tr("Presets:"), color_group));

  struct ColorPreset {
    const char *name;
    QColor color;
  };
  const ColorPreset presets[] = {
      {"Gray", QColor(128, 128, 140, 90)},
      {"Light", QColor(220, 220, 230, 90)},
      {"Dark", QColor(40, 40, 45, 110)},
      {"Blue", QColor(59, 130, 246, 100)},
      {"Emerald", QColor(16, 185, 129, 100)},
      {"Red", QColor(239, 68, 68, 100)},
  };

  for (const auto &p : presets) {
    auto *btn = new QPushButton(color_group);
    btn->setFixedSize(22, 22);
    btn->setToolTip(tr(p.name));
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid rgba(255,255,255,0.2); border-radius: 4px;")
                           .arg(p.color.name(QColor::HexArgb)));
    connect(btn, &QPushButton::clicked, this, [this, col = p.color]() {
      current_color_ = col;
      opacity_slider_->setValue(static_cast<int>(std::round(col.alphaF() * 100.0)));
      update_color_button();
      update_preview();
    });
    preset_row->addWidget(btn);
  }
  preset_row->addStretch(1);
  color_layout->addLayout(preset_row);

  root_layout->addWidget(color_group);

  // Buttons row
  auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);

  connect(btn_box, &QDialogButtonBox::accepted, this, [this]() {
    auto &settings = AppSettings::instance();
    settings.set_show_grid(show_grid_check_->isChecked());
    settings.set_grid_size(size_spin_->value());
    settings.set_grid_color(current_color_);
    accept();
  });

  connect(btn_box, &QDialogButtonBox::rejected, this, [this]() {
    // Revert to initial settings
    if (canvas_) {
      canvas_->set_show_grid(initial_show_);
      canvas_->set_grid_size(initial_size_);
      canvas_->set_grid_color(initial_color_);
    }
    reject();
  });

  auto *reset_btn = btn_box->button(QDialogButtonBox::RestoreDefaults);
  if (reset_btn) {
    connect(reset_btn, &QPushButton::clicked, this, [this]() {
      show_grid_check_->setChecked(false);
      size_spin_->setValue(32);
      current_color_ = QColor(128, 128, 140, 90);
      opacity_slider_->setValue(static_cast<int>(std::round((90.0 / 255.0) * 100.0)));
      update_color_button();
      update_preview();
    });
  }

  root_layout->addWidget(btn_box);
}

void GridSetupDialog::update_color_button() {
  if (color_btn_) {
    color_btn_->setStyleSheet(
        QString("background-color: %1; border: 1px solid rgba(255,255,255,0.2); border-radius: 4px; font-weight: bold;")
            .arg(current_color_.name(QColor::HexRgb)));
    color_btn_->setText(current_color_.name(QColor::HexRgb).toUpper());
  }
}

void GridSetupDialog::update_preview() {
  if (!canvas_) return;
  canvas_->set_show_grid(show_grid_check_->isChecked());
  canvas_->set_grid_size(size_spin_->value());
  canvas_->set_grid_color(core::Rgba8{
      static_cast<std::uint8_t>(current_color_.red()),
      static_cast<std::uint8_t>(current_color_.green()),
      static_cast<std::uint8_t>(current_color_.blue()),
      static_cast<std::uint8_t>(current_color_.alpha()),
  });
}

} // namespace kreart::ui
