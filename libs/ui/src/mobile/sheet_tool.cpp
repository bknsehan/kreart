#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <vector>

#include "kreart/ui/app_settings.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"
#include "kreart/ui/pressure_curve.h"

namespace kreart::ui::mobile {
namespace {

// Segmented control: a row of checkable buttons where exactly one is active.
QWidget *make_segmented(QWidget *parent, const QStringList &labels, int initial,
                        std::function<void(int)> on_change,
                        std::vector<QPushButton *> *out_buttons = nullptr) {
  auto *host = new QWidget(parent);
  auto *layout = new QHBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);
  std::vector<QPushButton *> buttons;
  for (int i = 0; i < labels.size(); ++i) {
    auto *btn = new QPushButton(labels[i], host);
    btn->setCheckable(true);
    btn->setMinimumHeight(44);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QPushButton { background: #1E2127; color: #9AA0AD; border: none; border-radius: 12px;"
        " padding: 0 12px; font-size: 13px; }"
        "QPushButton:checked { background: #E8B44A; color: #1A1408; font-weight: 600; }");
    layout->addWidget(btn, 1);
    buttons.push_back(btn);
  }
  for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
    QObject::connect(buttons[i], &QPushButton::clicked, host, [buttons, i, on_change](bool) {
      for (int j = 0; j < static_cast<int>(buttons.size()); ++j) {
        buttons[j]->setChecked(j == i);
      }
      if (on_change) on_change(i);
    });
  }
  if (initial >= 0 && initial < static_cast<int>(buttons.size())) {
    buttons[initial]->setChecked(true);
  }
  if (out_buttons) *out_buttons = buttons;
  return host;
}

QWidget *make_slider_block(QWidget *parent, const QString &label, int min_value, int max_value,
                           int value, const QString &suffix,
                           std::function<void(int)> on_change) {
  auto *host = new QWidget(parent);
  auto *layout = new QVBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  auto *header = new QHBoxLayout();
  auto *name = new QLabel(label, host);
  name->setProperty("muted", true);
  auto *readout = new QLabel(QString::number(value) + suffix, host);
  readout->setFont(font_body());
  header->addWidget(name);
  header->addStretch(1);
  header->addWidget(readout);

  auto *slider = new QSlider(Qt::Horizontal, host);
  slider->setRange(min_value, max_value);
  slider->setValue(value);
  slider->setMinimumHeight(44);

  QObject::connect(slider, &QSlider::valueChanged, host, [readout, suffix, on_change](int v) {
    readout->setText(QString::number(v) + suffix);
    if (on_change) on_change(v);
  });

  layout->addLayout(header);
  layout->addWidget(slider);
  return host;
}

} // namespace

void build_tool_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  (void)sheets;
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  Canvas *canvas = ctx.canvas;

  // Size / opacity / smoothing
  layout->addWidget(make_slider_block(
      body, QObject::tr("Size"), 1, 500,
      canvas ? static_cast<int>(std::lround(canvas->brush_size())) : 32, " px",
      [canvas](int v) { if (canvas) canvas->set_brush_size(v); }));
  layout->addWidget(make_slider_block(
      body, QObject::tr("Opacity"), 1, 100,
      canvas ? static_cast<int>(std::lround(canvas->brush_settings().opacity * 100.0)) : 100, "%",
      [canvas](int v) { if (canvas) canvas->set_brush_opacity(v / 100.0); }));
  layout->addWidget(make_slider_block(
      body, QObject::tr("Smoothing"), 0, 100,
      canvas ? static_cast<int>(std::lround(canvas->brush_smoothing() * 100.0)) : 35, "%",
      [canvas](int v) { if (canvas) canvas->set_brush_smoothing(v / 100.0); }));

  auto *mode_label = new QLabel(QObject::tr("Smoothing mode"), body);
  mode_label->setProperty("muted", true);
  layout->addWidget(mode_label);
  int initial_mode = 2;
  if (canvas) {
    switch (canvas->smoothing_mode()) {
      case brush::SmoothingMode::NONE: initial_mode = 0; break;
      case brush::SmoothingMode::BASIC: initial_mode = 1; break;
      case brush::SmoothingMode::WEIGHTED: initial_mode = 2; break;
      case brush::SmoothingMode::STABILIZER: initial_mode = 3; break;
      default: initial_mode = 3; break;
    }
  }
  layout->addWidget(make_segmented(
      body, {QObject::tr("None"), QObject::tr("Basic"), QObject::tr("Weighted"), QObject::tr("Stabilizer")},
      initial_mode, [canvas](int idx) {
        if (!canvas) return;
        brush::SmoothingMode mode = brush::SmoothingMode::WEIGHTED;
        if (idx == 0) mode = brush::SmoothingMode::NONE;
        else if (idx == 1) mode = brush::SmoothingMode::BASIC;
        else if (idx == 2) mode = brush::SmoothingMode::WEIGHTED;
        else mode = brush::SmoothingMode::STABILIZER;
        canvas->set_smoothing_mode(mode);
      }));

  // Tilt dynamics
  auto *tilt = new QCheckBox(QObject::tr("Tilt dynamics"), body);
  tilt->setChecked(canvas && canvas->tilt_enabled());
  QObject::connect(tilt, &QCheckBox::toggled, body, [canvas](bool on) {
    if (canvas) canvas->set_tilt_enabled(on);
  });
  layout->addWidget(tilt);

  // Texture
  auto *texture = new QCheckBox(QObject::tr("Texture"), body);
  texture->setChecked(canvas && canvas->texture_enabled());
  layout->addWidget(texture);

  auto *pattern_row = new QWidget(body);
  auto *pattern_layout = new QVBoxLayout(pattern_row);
  pattern_layout->setContentsMargins(0, 0, 0, 0);
  pattern_layout->setSpacing(6);
  auto *pattern_label = new QLabel(QObject::tr("Pattern"), pattern_row);
  pattern_label->setProperty("muted", true);
  pattern_layout->addWidget(pattern_label);
  auto *pattern_buttons = new QWidget(pattern_row);
  auto *pattern_button_row = new QHBoxLayout(pattern_buttons);
  pattern_button_row->setContentsMargins(0, 0, 0, 0);
  pattern_button_row->setSpacing(6);
  const QStringList pattern_names = {QObject::tr("Rough Paper"), QObject::tr("Canvas Grain"),
                                     QObject::tr("Charcoal Tooth")};
  for (int i = 0; i < pattern_names.size(); ++i) {
    auto *btn = new QPushButton(pattern_names[i], pattern_buttons);
    btn->setMinimumHeight(44);
    btn->setCursor(Qt::PointingHandCursor);
    QObject::connect(btn, &QPushButton::clicked, pattern_buttons, [canvas, i, pattern_names, pattern_buttons](bool) {
      if (!canvas) return;
      brush::TextureMaskPtr mask;
      if (i == 0) mask = brush::TextureMask::create_rough_paper();
      else if (i == 1) mask = brush::TextureMask::create_canvas_grain();
      else mask = brush::TextureMask::create_charcoal_tooth();
      canvas->set_texture_pattern(mask);
      canvas->set_texture_enabled(true);
      (void)pattern_names;
      (void)pattern_buttons;
    });
    pattern_button_row->addWidget(btn, 1);
  }
  pattern_layout->addWidget(pattern_buttons);
  pattern_layout->addWidget(make_slider_block(
      pattern_row, QObject::tr("Scale"), 10, 300,
      canvas ? static_cast<int>(std::lround(canvas->texture_scale() * 100.0)) : 100, "%",
      [canvas](int v) { if (canvas) canvas->set_texture_scale(v / 100.0); }));
  pattern_layout->addWidget(make_slider_block(
      pattern_row, QObject::tr("Strength"), 0, 100,
      canvas ? static_cast<int>(std::lround(canvas->texture_strength() * 100.0)) : 75, "%",
      [canvas](int v) { if (canvas) canvas->set_texture_strength(v / 100.0); }));
  layout->addWidget(pattern_row);

  auto sync_texture = [texture, pattern_row, canvas]() {
    pattern_row->setVisible(texture->isChecked());
    (void)canvas;
  };
  QObject::connect(texture, &QCheckBox::toggled, body, [canvas, sync_texture](bool on) {
    if (canvas) canvas->set_texture_enabled(on);
    sync_texture();
  });
  sync_texture();

  // Smudge rate (only meaningful for the smudge tool)
  auto *smudge_row = new QWidget(body);
  auto *smudge_layout = new QVBoxLayout(smudge_row);
  smudge_layout->setContentsMargins(0, 0, 0, 0);
  smudge_layout->setSpacing(2);
  smudge_layout->addWidget(make_slider_block(
      smudge_row, QObject::tr("Smudge rate"), 0, 100,
      canvas ? static_cast<int>(std::lround(canvas->brush_smudge_rate() * 100.0)) : 50, "%",
      [canvas](int v) { if (canvas) canvas->set_brush_smudge_rate(v / 100.0); }));
  smudge_layout->addWidget(make_slider_block(
      smudge_row, QObject::tr("Color rate"), 0, 100,
      canvas ? static_cast<int>(std::lround(canvas->brush_color_rate() * 100.0)) : 100, "%",
      [canvas](int v) { if (canvas) canvas->set_brush_color_rate(v / 100.0); }));
  layout->addWidget(smudge_row);
  smudge_row->setVisible(canvas && canvas->tool() == Canvas::Tool::SMUDGE);

  // Magic wand
  auto *wand_label = new QLabel(QObject::tr("Magic wand"), body);
  wand_label->setProperty("muted", true);
  layout->addWidget(wand_label);
  layout->addWidget(make_slider_block(
      body, QObject::tr("Tolerance"), 0, 255, canvas ? canvas->wand_tolerance() : 32, "",
      [canvas](int v) { if (canvas) canvas->set_wand_tolerance(v); }));
  auto *contiguous = new QCheckBox(QObject::tr("Contiguous"), body);
  contiguous->setChecked(canvas && canvas->wand_contiguous());
  QObject::connect(contiguous, &QCheckBox::toggled, body, [canvas](bool on) {
    if (canvas) canvas->set_wand_contiguous(on);
  });
  layout->addWidget(contiguous);

  auto *lasso_label = new QLabel(QObject::tr("Lasso mode"), body);
  lasso_label->setProperty("muted", true);
  layout->addWidget(lasso_label);
  layout->addWidget(make_segmented(
      body, {QObject::tr("Freehand"), QObject::tr("Magic Object")},
      canvas && canvas->lasso_mode() == Canvas::LassoMode::MAGIC_OBJECT ? 1 : 0,
      [canvas](int idx) {
        if (canvas) {
          canvas->set_lasso_mode(idx == 1 ? Canvas::LassoMode::MAGIC_OBJECT
                                          : Canvas::LassoMode::FREEHAND);
        }
      }));

  // Symmetry
  auto *sym_v = new QCheckBox(QObject::tr("Vertical symmetry"), body);
  sym_v->setChecked(canvas && canvas->symmetry_vertical());
  QObject::connect(sym_v, &QCheckBox::toggled, body, [canvas](bool on) {
    if (!canvas) return;
    if (canvas->symmetry_vertical() != on) canvas->toggle_symmetry_vertical();
  });
  layout->addWidget(sym_v);

  auto *sym_h = new QCheckBox(QObject::tr("Horizontal symmetry"), body);
  sym_h->setChecked(canvas && canvas->symmetry_horizontal());
  QObject::connect(sym_h, &QCheckBox::toggled, body, [canvas](bool on) {
    if (!canvas) return;
    if (canvas->symmetry_horizontal() != on) canvas->toggle_symmetry_horizontal();
  });
  layout->addWidget(sym_h);

  // Pressure curve. Desktop never pushes the saved curve into Canvas, so this is
  // the wiring that makes the control effective on mobile.
  auto *curve_label = new QLabel(QObject::tr("Pressure curve"), body);
  curve_label->setProperty("muted", true);
  layout->addWidget(curve_label);
  auto *curve = new PressureCurve(body);
  curve->setMinimumHeight(220);
  bool ok = false;
  const double saved_gamma = AppSettings::instance().pressure_curve().toDouble(&ok);
  if (ok && saved_gamma > 0.0) {
    curve->set_gamma(saved_gamma);
    if (canvas) canvas->set_pressure_curve(saved_gamma);
  }
  QObject::connect(curve, &PressureCurve::curveChanged, body, [canvas](double gamma) {
    if (std::abs(gamma - 1.0) < 0.001) {
      AppSettings::instance().set_pressure_curve("0.0,0.0;1.0,1.0");
    } else {
      AppSettings::instance().set_pressure_curve(QString::number(gamma, 'f', 3));
    }
    if (canvas) canvas->set_pressure_curve(gamma);
  });
  layout->addWidget(curve);

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);
}

} // namespace kreart::ui::mobile
