#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <memory>

#include "kreart/ui/app_settings.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

QPushButton *make_icon_button(QWidget *parent, const QString &icon, const QString &tooltip,
                              std::function<void()> fn) {
  auto *btn = new QPushButton(parent);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setToolTip(tooltip);
  btn->setIcon(IconHelper::load_symbolic(icon, tokens().text, 22));
  btn->setIconSize(QSize(22, 22));
  // min-width/max-width are needed because the sheet QSS sets min-height on
  // every QPushButton, which would otherwise stretch these square controls.
  btn->setStyleSheet("QPushButton { background: #1E2127; border: none; border-radius: 14px;"
                     " min-width: 52px; max-width: 52px; min-height: 52px; max-height: 52px; }"
                     "QPushButton:pressed { background: #2A2E36; }");
  QObject::connect(btn, &QPushButton::clicked, parent, [fn]() { if (fn) fn(); });
  return btn;
}

} // namespace

void build_view_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  (void)sheets;
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  Canvas *canvas = ctx.canvas;

  // Zoom / rotate / mirror row
  auto *row = new QHBoxLayout();
  row->setSpacing(8);
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(make_icon_button(body, ":/icons/icons/list-remove-symbolic.svg",
                                  QObject::tr("Zoom out"), [canvas]() {
                                    if (canvas) canvas->zoom_out();
                                  }));
  row->addWidget(make_icon_button(body, ":/icons/icons/list-add-symbolic.svg",
                                  QObject::tr("Zoom in"), [canvas]() {
                                    if (canvas) canvas->zoom_in();
                                  }));
  row->addWidget(make_icon_button(body, ":/icons/icons/pan-end-symbolic.svg",
                                  QObject::tr("Fit"), [canvas]() {
                                    if (canvas) canvas->zoom_fit();
                                  }));
  row->addWidget(make_icon_button(body, ":/icons/icons/edit-undo-symbolic.svg",
                                  QObject::tr("Rotate -15"), [canvas]() {
                                    if (canvas) canvas->rotate_by(-M_PI / 12.0);
                                  }));
  row->addWidget(make_icon_button(body, ":/icons/icons/edit-redo-symbolic.svg",
                                  QObject::tr("Rotate +15"), [canvas]() {
                                    if (canvas) canvas->rotate_by(M_PI / 12.0);
                                  }));
  row->addWidget(make_icon_button(body, ":/icons/icons/symmetry-h-symbolic.svg",
                                  QObject::tr("Reset rotation"), [canvas]() {
                                    if (canvas) canvas->reset_rotation();
                                  }));
  row->addStretch(1);
  layout->addLayout(row);

  auto *mirror_h = new QCheckBox(QObject::tr("Mirror horizontal"), body);
  mirror_h->setChecked(canvas && canvas->mirror_horizontal());
  QObject::connect(mirror_h, &QCheckBox::toggled, body, [canvas](bool on) {
    if (canvas) canvas->set_mirror_horizontal(on);
  });
  layout->addWidget(mirror_h);

  auto *mirror_v = new QCheckBox(QObject::tr("Mirror vertical"), body);
  mirror_v->setChecked(canvas && canvas->mirror_vertical());
  QObject::connect(mirror_v, &QCheckBox::toggled, body, [canvas](bool on) {
    if (canvas) canvas->set_mirror_vertical(on);
  });
  layout->addWidget(mirror_v);

  auto *pixelated = new QCheckBox(QObject::tr("Pixelated"), body);
  pixelated->setChecked(canvas && canvas->pixelated());
  QObject::connect(pixelated, &QCheckBox::toggled, body, [canvas](bool on) {
    if (!canvas) return;
    canvas->set_pixelated(on);
    // View-only change: repaint without rebuilding composites.
    canvas->update();
  });
  layout->addWidget(pixelated);

  auto *grid = new QCheckBox(QObject::tr("Show grid"), body);
  grid->setChecked(canvas && canvas->show_grid());
  layout->addWidget(grid);

  auto *spacing_host = new QWidget(body);
  auto *spacing_layout = new QVBoxLayout(spacing_host);
  spacing_layout->setContentsMargins(0, 0, 0, 0);
  spacing_layout->setSpacing(2);
  auto *spacing_header = new QHBoxLayout();
  auto *spacing_label = new QLabel(QObject::tr("Grid spacing"), spacing_host);
  spacing_label->setProperty("muted", true);
  auto *spacing_readout = new QLabel(
      QString::number(canvas ? canvas->grid_size() : 32) + " px", spacing_host);
  spacing_readout->setFont(font_body());
  spacing_header->addWidget(spacing_label);
  spacing_header->addStretch(1);
  spacing_header->addWidget(spacing_readout);
  auto *spacing_slider = new QSlider(Qt::Horizontal, spacing_host);
  spacing_slider->setRange(4, 256);
  spacing_slider->setValue(canvas ? canvas->grid_size() : 32);
  spacing_slider->setMinimumHeight(44);
  spacing_layout->addLayout(spacing_header);
  spacing_layout->addWidget(spacing_slider);
  layout->addWidget(spacing_host);

  QObject::connect(spacing_slider, &QSlider::valueChanged, body,
                   [canvas, spacing_readout](int v) {
                     spacing_readout->setText(QString::number(v) + " px");
                     if (!canvas) return;
                     canvas->set_grid_size(v);
                     // Preview only: no composite rebuild per slider tick.
                     canvas->update();
                   });
  QObject::connect(grid, &QCheckBox::toggled, body, [canvas, spacing_host](bool on) {
    if (canvas) canvas->set_show_grid(on);
    spacing_host->setVisible(on);
  });
  spacing_host->setVisible(grid->isChecked());

  // Grid colour
  auto *color_row = new QHBoxLayout();
  auto *color_label = new QLabel(QObject::tr("Grid colour"), body);
  color_label->setProperty("muted", true);
  auto *color_btn = new QPushButton(body);
  color_btn->setFixedSize(44, 44);
  auto update_color_button = [canvas, color_btn]() {
    const core::Rgba8 c = canvas ? canvas->grid_color() : core::Rgba8{128, 128, 140, 90};
    color_btn->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4); border: 1px solid"
                                     " rgba(255,255,255,0.25); border-radius: 10px;")
                                 .arg(c.r).arg(c.g).arg(c.b).arg(c.a));
  };
  update_color_button();
  QObject::connect(color_btn, &QPushButton::clicked, body,
                   [&ctx, &sheets, canvas, update_color_button]() {
                     // Dedicated RGBA sheet so picking a grid colour never
                     // disturbs the active brush colour.
                     auto *picker_sheet = new MobileSheet(sheets.host(), QObject::tr("Grid Colour"), true);
                     auto *picker_body = new QWidget(picker_sheet);
                     auto *picker_layout = new QVBoxLayout(picker_body);
                     picker_layout->setContentsMargins(0, 0, 0, 0);
                     picker_layout->setSpacing(8);

                     const core::Rgba8 initial =
                         canvas ? canvas->grid_color() : core::Rgba8{128, 128, 140, 90};
                     auto current = std::make_shared<core::Rgba8>(initial);
                     auto push = [canvas, current]() {
                       if (canvas) {
                         canvas->set_grid_color(*current);
                         canvas->update();
                       }
                     };

                     auto add_channel = [&](const QString &label, std::uint8_t core::Rgba8::*field,
                                            int value) {
                       auto *host = new QWidget(picker_body);
                       auto *host_layout = new QVBoxLayout(host);
                       host_layout->setContentsMargins(0, 0, 0, 0);
                       host_layout->setSpacing(2);
                       auto *header = new QHBoxLayout();
                       auto *name = new QLabel(label, host);
                       name->setProperty("muted", true);
                       auto *readout = new QLabel(QString::number(value), host);
                       readout->setFont(font_body());
                       header->addWidget(name);
                       header->addStretch(1);
                       header->addWidget(readout);
                       auto *slider = new QSlider(Qt::Horizontal, host);
                       slider->setRange(0, 255);
                       slider->setValue(value);
                       slider->setMinimumHeight(44);
                       host_layout->addLayout(header);
                       host_layout->addWidget(slider);
                       QObject::connect(slider, &QSlider::valueChanged, host,
                                        [readout, current, field, push](int v) {
                                          readout->setText(QString::number(v));
                                          (*current).*field = static_cast<std::uint8_t>(v);
                                          push();
                                        });
                       picker_layout->addWidget(host);
                     };
                     add_channel(QObject::tr("Red"), &core::Rgba8::r, initial.r);
                     add_channel(QObject::tr("Green"), &core::Rgba8::g, initial.g);
                     add_channel(QObject::tr("Blue"), &core::Rgba8::b, initial.b);
                     add_channel(QObject::tr("Alpha"), &core::Rgba8::a, initial.a);

                     picker_sheet->set_body(picker_body);
                     picker_sheet->set_primary_action(QObject::tr("Done"), [picker_sheet, update_color_button]() {
                       update_color_button();
                       picker_sheet->close();
                     });
                     sheets.register_transient(picker_sheet);
                     sheets.show_sheet_public(picker_sheet);
                     (void)ctx;
                   });
  color_row->addWidget(color_label);
  color_row->addWidget(color_btn);
  color_row->addStretch(1);
  layout->addLayout(color_row);

  // Transparency
  auto *trans_label = new QLabel(QObject::tr("Transparency"), body);
  trans_label->setFont(font_section());
  layout->addWidget(trans_label);

  auto *follow = new QCheckBox(QObject::tr("Follow device"), body);
  follow->setChecked(AppSettings::instance().transparency_follow_device());
  layout->addWidget(follow);

  auto *mode_combo = new QComboBox(body);
  mode_combo->addItems({QObject::tr("Light checker"), QObject::tr("Dark checker"),
                        QObject::tr("Solid colour")});
  switch (AppSettings::instance().transparency_mode()) {
    case TransparencyMode::LIGHT_CHECKERBOARD: mode_combo->setCurrentIndex(0); break;
    case TransparencyMode::DARK_CHECKERBOARD: mode_combo->setCurrentIndex(1); break;
    case TransparencyMode::SOLID_COLOR: mode_combo->setCurrentIndex(2); break;
  }
  mode_combo->setEnabled(!follow->isChecked());
  layout->addWidget(mode_combo);

  QObject::connect(follow, &QCheckBox::toggled, body, [mode_combo](bool on) {
    AppSettings::instance().set_transparency_follow_device(on);
    mode_combo->setEnabled(!on);
  });
  QObject::connect(mode_combo, &QComboBox::currentIndexChanged, body, [](int idx) {
    if (idx == 0) AppSettings::instance().set_transparency_mode(TransparencyMode::LIGHT_CHECKERBOARD);
    else if (idx == 1) AppSettings::instance().set_transparency_mode(TransparencyMode::DARK_CHECKERBOARD);
    else AppSettings::instance().set_transparency_mode(TransparencyMode::SOLID_COLOR);
  });

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);
}

} // namespace kreart::ui::mobile
