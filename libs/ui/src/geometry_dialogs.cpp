#include "kreart/ui/geometry_dialogs.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <cmath>

namespace kreart::ui {

// ----------------------------------------------------------------------------
// Canvas Size Dialog
// ----------------------------------------------------------------------------
CanvasSizeDialog::CanvasSizeDialog(QWidget *parent, core::Document &document, Canvas *canvas)
    : QDialog(parent), document_(document), canvas_(canvas) {
  setWindowTitle(tr("Canvas Size"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(10);

  auto *info_lbl = new QLabel(tr("Current size: %1 × %2 px")
                                  .arg(document_.width())
                                  .arg(document_.height()), this);
  layout->addWidget(info_lbl);

  // Width
  auto *w_row = new QHBoxLayout();
  w_row->addWidget(new QLabel(tr("Width:"), this));
  width_spin_ = new QSpinBox(this);
  width_spin_->setRange(16, 16384);
  width_spin_->setValue(document_.width());
  w_row->addWidget(width_spin_);
  layout->addLayout(w_row);

  // Height
  auto *h_row = new QHBoxLayout();
  h_row->addWidget(new QLabel(tr("Height:"), this));
  height_spin_ = new QSpinBox(this);
  height_spin_->setRange(16, 16384);
  height_spin_->setValue(document_.height());
  h_row->addWidget(height_spin_);
  layout->addLayout(h_row);

  // Anchor
  auto *a_row = new QHBoxLayout();
  a_row->addWidget(new QLabel(tr("Anchor:"), this));
  anchor_combo_ = new QComboBox(this);
  anchor_combo_->addItem(tr("Center"));
  anchor_combo_->addItem(tr("Top-Left"));
  anchor_combo_->addItem(tr("Top-Right"));
  anchor_combo_->addItem(tr("Bottom-Left"));
  anchor_combo_->addItem(tr("Bottom-Right"));
  anchor_combo_->setCurrentIndex(0);
  a_row->addWidget(anchor_combo_);
  layout->addLayout(a_row);

  // Buttons
  auto *btn_row = new QHBoxLayout();
  btn_row->addStretch(1);

  auto *cancel_btn = new QPushButton(tr("Cancel"), this);
  connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
  btn_row->addWidget(cancel_btn);

  auto *apply_btn = new QPushButton(tr("Apply"), this);
  apply_btn->setDefault(true);
  connect(apply_btn, &QPushButton::clicked, this, [this]() {
    const int new_w = width_spin_->value();
    const int new_h = height_spin_->value();
    const int old_w = document_.width();
    const int old_h = document_.height();
    const int anchor_idx = anchor_combo_->currentIndex();

    int offset_x = (new_w - old_w) / 2;
    int offset_y = (new_h - old_h) / 2;
    if (anchor_idx == 1) { // Top-Left
      offset_x = 0; offset_y = 0;
    } else if (anchor_idx == 2) { // Top-Right
      offset_x = new_w - old_w; offset_y = 0;
    } else if (anchor_idx == 3) { // Bottom-Left
      offset_x = 0; offset_y = new_h - old_h;
    } else if (anchor_idx == 4) { // Bottom-Right
      offset_x = new_w - old_w; offset_y = new_h - old_h;
    }

    document_.resize_canvas(new_w, new_h, offset_x, offset_y);
    if (canvas_) {
      canvas_->reposition_symmetry_axes();
      canvas_->refresh();
      canvas_->zoom_fit();
    }
  });
  btn_row->addWidget(apply_btn);
  layout->addLayout(btn_row);
}

// ----------------------------------------------------------------------------
// Scale Image Dialog
// ----------------------------------------------------------------------------
ScaleImageDialog::ScaleImageDialog(QWidget *parent, core::Document &document, Canvas *canvas)
    : QDialog(parent), document_(document), canvas_(canvas) {
  setWindowTitle(tr("Scale Image (Resample)"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(10);

  auto *info_lbl = new QLabel(tr("Current size: %1 × %2 px")
                                  .arg(document_.width())
                                  .arg(document_.height()), this);
  info_lbl->setStyleSheet("color: gray;");
  layout->addWidget(info_lbl);

  const double aspect = static_cast<double>(document_.width()) / std::max(1, document_.height());

  // Width
  auto *w_row = new QHBoxLayout();
  w_row->addWidget(new QLabel(tr("Width:"), this));
  width_spin_ = new QSpinBox(this);
  width_spin_->setRange(16, 16384);
  width_spin_->setValue(document_.width());
  w_row->addWidget(width_spin_);
  layout->addLayout(w_row);

  // Height
  auto *h_row = new QHBoxLayout();
  h_row->addWidget(new QLabel(tr("Height:"), this));
  height_spin_ = new QSpinBox(this);
  height_spin_->setRange(16, 16384);
  height_spin_->setValue(document_.height());
  h_row->addWidget(height_spin_);
  layout->addLayout(h_row);

  constrain_check_ = new QCheckBox(tr("Constrain proportions"), this);
  constrain_check_->setChecked(true);
  layout->addWidget(constrain_check_);

  connect(width_spin_, &QSpinBox::valueChanged, this, [this, aspect](int val) {
    if (guard_ || !constrain_check_->isChecked()) return;
    guard_ = true;
    height_spin_->setValue(static_cast<int>(std::round(val / aspect)));
    guard_ = false;
  });

  connect(height_spin_, &QSpinBox::valueChanged, this, [this, aspect](int val) {
    if (guard_ || !constrain_check_->isChecked()) return;
    guard_ = true;
    width_spin_->setValue(static_cast<int>(std::round(val * aspect)));
    guard_ = false;
  });

  // Presets row: 25%, 50%, 200%, 400%
  auto *presets_row = new QHBoxLayout();
  presets_row->addWidget(new QLabel(tr("Presets:"), this));
  auto add_pct_btn = [this](QHBoxLayout *row, const QString &text, double factor) {
    auto *btn = new QPushButton(text, this);
    connect(btn, &QPushButton::clicked, this, [this, factor]() {
      guard_ = true;
      width_spin_->setValue(std::max(16, static_cast<int>(std::round(document_.width() * factor))));
      height_spin_->setValue(std::max(16, static_cast<int>(std::round(document_.height() * factor))));
      guard_ = false;
    });
    row->addWidget(btn);
  };
  add_pct_btn(presets_row, "25%", 0.25);
  add_pct_btn(presets_row, "50%", 0.50);
  add_pct_btn(presets_row, "200%", 2.0);
  add_pct_btn(presets_row, "400%", 4.0);
  presets_row->addStretch(1);
  layout->addLayout(presets_row);

  // Filter dropdown
  auto *f_row = new QHBoxLayout();
  f_row->addWidget(new QLabel(tr("Filter:"), this));
  filter_combo_ = new QComboBox(this);
  filter_combo_->addItem(tr("Nearest Neighbor (Pixel Art)"));
  filter_combo_->addItem(tr("Area Box (Pixel Art Downscale)"));
  filter_combo_->addItem(tr("Bilinear (Smooth)"));
  filter_combo_->addItem(tr("Bicubic (Sharp Illustration)"));
  filter_combo_->addItem(tr("Lanczos3 (High Fidelity)"));
  filter_combo_->setCurrentIndex(3); // Bicubic
  f_row->addWidget(filter_combo_);
  layout->addLayout(f_row);

  // Buttons
  auto *btn_row = new QHBoxLayout();
  btn_row->addStretch(1);

  auto *cancel_btn = new QPushButton(tr("Cancel"), this);
  connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
  btn_row->addWidget(cancel_btn);

  auto *scale_btn = new QPushButton(tr("Scale"), this);
  scale_btn->setDefault(true);
  connect(scale_btn, &QPushButton::clicked, this, [this]() {
    const int new_w = width_spin_->value();
    const int new_h = height_spin_->value();
    const int sel = filter_combo_->currentIndex();
    static const core::ScaleFilter kFilters[] = {
        core::ScaleFilter::NEAREST,
        core::ScaleFilter::BOX,
        core::ScaleFilter::BILINEAR,
        core::ScaleFilter::BICUBIC,
        core::ScaleFilter::LANCZOS3};
    const auto filter = (sel >= 0 && sel < 5) ? kFilters[sel] : core::ScaleFilter::BICUBIC;

    document_.scale_image(new_w, new_h, filter);
    if (canvas_) {
      canvas_->reposition_symmetry_axes();
      canvas_->refresh();
      canvas_->zoom_fit();
    }
  });
  btn_row->addWidget(scale_btn);
  layout->addLayout(btn_row);
}

} // namespace kreart::ui
