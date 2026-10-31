#include "kreart/ui/filter_dialogs.h"

#include <QHBoxLayout>
#include <QFrame>
#include <cmath>
#include <cstdio>

namespace kreart::ui {
namespace {

QHBoxLayout *make_slider_row(const QString &label_text, QSlider *slider, QLabel *val_label) {
  auto *row = new QHBoxLayout();
  auto *lbl = new QLabel(label_text);
  lbl->setFixedWidth(120);
  row->addWidget(lbl);

  row->addWidget(slider);

  val_label->setFixedWidth(60);
  val_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  row->addWidget(val_label);
  return row;
}

} // namespace

BaseFilterDialog::BaseFilterDialog(QWidget *parent, core::Document &document,
                                   Canvas &canvas, const QString &title)
    : QDialog(parent), document_(document), canvas_(canvas), original_image_(document.active_layer().image()) {
  setWindowTitle(title);
  setModal(true);
  document_.begin_change();
}

BaseFilterDialog::~BaseFilterDialog() {
  if (!is_committed_) {
    document_.active_layer().image() = original_image_;
    document_.cancel_change();
    canvas_.refresh();
  }
}

void BaseFilterDialog::setup_dialog(QWidget *content_widget) {
  auto *vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(16, 16, 16, 16);
  vbox->setSpacing(12);

  vbox->addWidget(content_widget);

  auto *line = new QFrame(this);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  vbox->addWidget(line);

  auto *btn_bar = new QHBoxLayout();
  btn_bar->setSpacing(8);

  preview_check_ = new QCheckBox(tr("Preview"), this);
  preview_check_->setChecked(true);
  connect(preview_check_, &QCheckBox::toggled, this, &BaseFilterDialog::update_preview);
  btn_bar->addWidget(preview_check_);

  reset_btn_ = new QPushButton(tr("Reset"), this);
  connect(reset_btn_, &QPushButton::clicked, this, [this]() {
    reset_params();
    update_preview();
  });
  btn_bar->addWidget(reset_btn_);

  btn_bar->addStretch(1);

  cancel_btn_ = new QPushButton(tr("Cancel"), this);
  connect(cancel_btn_, &QPushButton::clicked, this, [this]() {
    document_.active_layer().image() = original_image_;
    document_.cancel_change();
    canvas_.refresh();
    reject();
  });
  btn_bar->addWidget(cancel_btn_);

  apply_btn_ = new QPushButton(tr("Apply"), this);
  apply_btn_->setDefault(true);
  connect(apply_btn_, &QPushButton::clicked, this, [this]() {
    is_committed_ = true;
    document_.commit_change();
    canvas_.refresh();
    accept();
  });
  btn_bar->addWidget(apply_btn_);

  vbox->addLayout(btn_bar);
}

void BaseFilterDialog::update_preview() {
  if (updating_) return;
  updating_ = true;

  document_.active_layer().image() = original_image_;
  if (preview_check_->isChecked()) {
    apply_filter();
  }
  canvas_.refresh();

  updating_ = false;
}

// ----------------------------------------------------------------------------
// HSV Filter Dialog
// ----------------------------------------------------------------------------
HsvFilterDialog::HsvFilterDialog(QWidget *parent, core::Document &document, Canvas &canvas)
    : BaseFilterDialog(parent, document, canvas, tr("Hue / Saturation / Value")) {
  auto *content = new QWidget(this);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  hue_slider_ = new QSlider(Qt::Horizontal, content);
  hue_slider_->setRange(-180, 180);
  hue_slider_->setValue(0);
  hue_label_ = new QLabel("0°", content);
  connect(hue_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    hue_label_->setText(QString::number(val) + "°");
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Hue:"), hue_slider_, hue_label_));

  sat_slider_ = new QSlider(Qt::Horizontal, content);
  sat_slider_->setRange(-100, 100);
  sat_slider_->setValue(0);
  sat_label_ = new QLabel("0%", content);
  connect(sat_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    sat_label_->setText(QString::number(val) + "%");
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Saturation:"), sat_slider_, sat_label_));

  val_slider_ = new QSlider(Qt::Horizontal, content);
  val_slider_->setRange(-100, 100);
  val_slider_->setValue(0);
  val_label_ = new QLabel("0%", content);
  connect(val_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    val_label_->setText(QString::number(val) + "%");
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Value / Lightness:"), val_slider_, val_label_));

  setup_dialog(content);
}

void HsvFilterDialog::apply_filter() {
  const double h = hue_slider_->value();
  const double s = sat_slider_->value() / 100.0;
  const double v = val_slider_->value() / 100.0;
  core::filter_hsv(document_.active_layer().image(), h, s, v,
                   document_.selection().active() ? &document_.selection() : nullptr);
}

void HsvFilterDialog::reset_params() {
  updating_ = true;
  hue_slider_->setValue(0);
  hue_label_->setText("0°");
  sat_slider_->setValue(0);
  sat_label_->setText("0%");
  val_slider_->setValue(0);
  val_label_->setText("0%");
  updating_ = false;
}

// ----------------------------------------------------------------------------
// Brightness & Contrast Dialog
// ----------------------------------------------------------------------------
BrightnessContrastDialog::BrightnessContrastDialog(QWidget *parent, core::Document &document, Canvas &canvas)
    : BaseFilterDialog(parent, document, canvas, tr("Brightness & Contrast")) {
  auto *content = new QWidget(this);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  brightness_slider_ = new QSlider(Qt::Horizontal, content);
  brightness_slider_->setRange(-100, 100);
  brightness_slider_->setValue(0);
  brightness_label_ = new QLabel("0", content);
  connect(brightness_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    brightness_label_->setText(QString::number(val));
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Brightness:"), brightness_slider_, brightness_label_));

  contrast_slider_ = new QSlider(Qt::Horizontal, content);
  contrast_slider_->setRange(-100, 100);
  contrast_slider_->setValue(0);
  contrast_label_ = new QLabel("0", content);
  connect(contrast_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    contrast_label_->setText(QString::number(val));
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Contrast:"), contrast_slider_, contrast_label_));

  setup_dialog(content);
}

void BrightnessContrastDialog::apply_filter() {
  const double b = brightness_slider_->value();
  const double c = contrast_slider_->value();
  core::filter_brightness_contrast(document_.active_layer().image(), b, c,
                                   document_.selection().active() ? &document_.selection() : nullptr);
}

void BrightnessContrastDialog::reset_params() {
  updating_ = true;
  brightness_slider_->setValue(0);
  brightness_label_->setText("0");
  contrast_slider_->setValue(0);
  contrast_label_->setText("0");
  updating_ = false;
}

// ----------------------------------------------------------------------------
// Color Balance Dialog
// ----------------------------------------------------------------------------
ColorBalanceDialog::ColorBalanceDialog(QWidget *parent, core::Document &document, Canvas &canvas)
    : BaseFilterDialog(parent, document, canvas, tr("Color Balance")) {
  auto *content = new QWidget(this);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto *tone_row = new QHBoxLayout();
  tone_row->addWidget(new QLabel(tr("Tone Range:"), content));

  tone_combo_ = new QComboBox(content);
  tone_combo_->addItem(tr("Shadows"));
  tone_combo_->addItem(tr("Midtones"));
  tone_combo_->addItem(tr("Highlights"));
  tone_combo_->setCurrentIndex(1); // Midtones
  connect(tone_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    active_tone_ = index;
    sync_sliders();
  });
  tone_row->addWidget(tone_combo_);
  layout->addLayout(tone_row);

  cr_slider_ = new QSlider(Qt::Horizontal, content);
  cr_slider_->setRange(-100, 100);
  cr_label_ = new QLabel("0", content);
  connect(cr_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    if (active_tone_ == 0) params_.shadows.cyan_red = val;
    else if (active_tone_ == 1) params_.midtones.cyan_red = val;
    else params_.highlights.cyan_red = val;
    cr_label_->setText(QString::number(val));
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Cyan / Red:"), cr_slider_, cr_label_));

  mg_slider_ = new QSlider(Qt::Horizontal, content);
  mg_slider_->setRange(-100, 100);
  mg_label_ = new QLabel("0", content);
  connect(mg_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    if (active_tone_ == 0) params_.shadows.magenta_green = val;
    else if (active_tone_ == 1) params_.midtones.magenta_green = val;
    else params_.highlights.magenta_green = val;
    mg_label_->setText(QString::number(val));
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Magenta / Green:"), mg_slider_, mg_label_));

  yb_slider_ = new QSlider(Qt::Horizontal, content);
  yb_slider_->setRange(-100, 100);
  yb_label_ = new QLabel("0", content);
  connect(yb_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    if (active_tone_ == 0) params_.shadows.yellow_blue = val;
    else if (active_tone_ == 1) params_.midtones.yellow_blue = val;
    else params_.highlights.yellow_blue = val;
    yb_label_->setText(QString::number(val));
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Yellow / Blue:"), yb_slider_, yb_label_));

  preserve_lum_check_ = new QCheckBox(tr("Preserve Luminosity"), content);
  preserve_lum_check_->setChecked(true);
  connect(preserve_lum_check_, &QCheckBox::toggled, this, [this](bool checked) {
    params_.preserve_luminosity = checked;
    update_preview();
  });
  layout->addWidget(preserve_lum_check_);

  sync_sliders();
  setup_dialog(content);
}

void ColorBalanceDialog::sync_sliders() {
  updating_ = true;
  double cr = 0.0, mg = 0.0, yb = 0.0;
  if (active_tone_ == 0) {
    cr = params_.shadows.cyan_red;
    mg = params_.shadows.magenta_green;
    yb = params_.shadows.yellow_blue;
  } else if (active_tone_ == 1) {
    cr = params_.midtones.cyan_red;
    mg = params_.midtones.magenta_green;
    yb = params_.midtones.yellow_blue;
  } else {
    cr = params_.highlights.cyan_red;
    mg = params_.highlights.magenta_green;
    yb = params_.highlights.yellow_blue;
  }

  cr_slider_->setValue(static_cast<int>(cr));
  mg_slider_->setValue(static_cast<int>(mg));
  yb_slider_->setValue(static_cast<int>(yb));

  cr_label_->setText(QString::number(static_cast<int>(cr)));
  mg_label_->setText(QString::number(static_cast<int>(mg)));
  yb_label_->setText(QString::number(static_cast<int>(yb)));
  updating_ = false;
}

void ColorBalanceDialog::apply_filter() {
  core::filter_color_balance(document_.active_layer().image(), params_,
                             document_.selection().active() ? &document_.selection() : nullptr);
}

void ColorBalanceDialog::reset_params() {
  params_ = core::ColorBalanceParams{};
  sync_sliders();
}

// ----------------------------------------------------------------------------
// Gaussian Blur Dialog
// ----------------------------------------------------------------------------
GaussianBlurDialog::GaussianBlurDialog(QWidget *parent, core::Document &document, Canvas &canvas)
    : BaseFilterDialog(parent, document, canvas, tr("Gaussian Blur")) {
  auto *content = new QWidget(this);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  radius_slider_ = new QSlider(Qt::Horizontal, content);
  radius_slider_->setRange(5, 500); // 0.5 to 50.0 px (* 10)
  radius_slider_->setValue(50);
  radius_label_ = new QLabel("5.0 px", content);
  connect(radius_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_) return;
    const double r = val / 10.0;
    radius_label_->setText(QString::number(r, 'f', 1) + " px");
    update_preview();
  });
  layout->addLayout(make_slider_row(tr("Radius:"), radius_slider_, radius_label_));

  setup_dialog(content);
}

void GaussianBlurDialog::apply_filter() {
  const double r = radius_slider_->value() / 10.0;
  core::filter_gaussian_blur(document_.active_layer().image(), r,
                             document_.selection().active() ? &document_.selection() : nullptr);
}

void GaussianBlurDialog::reset_params() {
  updating_ = true;
  radius_slider_->setValue(50);
  radius_label_->setText("5.0 px");
  updating_ = false;
}

} // namespace kreart::ui
