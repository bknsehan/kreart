#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>
#include <string>

#include "kreart/ui/color_picker.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_color_picker.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

QPushButton *make_swatch(QWidget *parent, core::Rgba8 color) {
  auto *btn = new QPushButton(parent);
  btn->setFixedSize(44, 44);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4); border: 1px solid rgba(255,255,255,0.25);"
                             " border-radius: 10px;")
                         .arg(color.r).arg(color.g).arg(color.b).arg(color.a));
  return btn;
}

} // namespace

void build_color_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  (void)sheets;
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto *picker = new MobileColorPicker(body);
  picker->set_color(ctx.current_color());
  picker->setMaximumWidth(360);
  // Centre the picker; on a phone-width sheet this is a no-op, on a wide
  // landscape sheet it keeps the ring circular and thumb-reachable.
  auto *picker_row = new QHBoxLayout();
  picker_row->addStretch(1);
  picker_row->addWidget(picker);
  picker_row->addStretch(1);
  layout->addLayout(picker_row);

  // Hex entry + RGBA steppers
  auto *hex_row = new QHBoxLayout();
  hex_row->setSpacing(8);
  auto *hex_label = new QLabel(QObject::tr("Hex"), body);
  hex_label->setProperty("muted", true);
  auto *hex_edit = new QLineEdit(body);
  hex_edit->setMinimumHeight(44);
  hex_edit->setText(QString::fromStdString(
      StudioColorPicker::color_to_hex(picker->color())));
  hex_row->addWidget(hex_label);
  hex_row->addWidget(hex_edit, 1);
  layout->addLayout(hex_row);

  auto *rgba_row = new QHBoxLayout();
  rgba_row->setSpacing(8);
  auto make_channel = [&](const QString &name) {
    auto *col = new QVBoxLayout();
    col->setSpacing(4);
    auto *label = new QLabel(name, body);
    label->setProperty("muted", true);
    auto *edit = new QLineEdit(body);
    edit->setMinimumHeight(44);
    edit->setFixedWidth(64);
    edit->setAlignment(Qt::AlignCenter);
    col->addWidget(label);
    col->addWidget(edit);
    rgba_row->addLayout(col);
    return edit;
  };
  auto *r_edit = make_channel("R");
  auto *g_edit = make_channel("G");
  auto *b_edit = make_channel("B");
  auto *a_edit = make_channel("A");
  layout->addLayout(rgba_row);

  auto sync_readouts = [picker, hex_edit, r_edit, g_edit, b_edit, a_edit]() {
    const core::Rgba8 c = picker->color();
    hex_edit->setText(QString::fromStdString(StudioColorPicker::color_to_hex(c)).toUpper());
    r_edit->setText(QString::number(c.r));
    g_edit->setText(QString::number(c.g));
    b_edit->setText(QString::number(c.b));
    a_edit->setText(QString::number(c.a));
  };

  QObject::connect(picker, &MobileColorPicker::colorChanged, body, [sync_readouts](const core::Rgba8 &) {
    sync_readouts();
  });
  sync_readouts();

  QObject::connect(hex_edit, &QLineEdit::editingFinished, body, [picker, hex_edit]() {
    core::Rgba8 parsed{};
    if (StudioColorPicker::hex_to_color(hex_edit->text().toStdString(), parsed)) {
      picker->set_color(parsed, true);
    }
  });
  auto commit_channels = [picker, r_edit, g_edit, b_edit, a_edit]() {
    core::Rgba8 c{};
    c.r = static_cast<std::uint8_t>(std::clamp(r_edit->text().toInt(), 0, 255));
    c.g = static_cast<std::uint8_t>(std::clamp(g_edit->text().toInt(), 0, 255));
    c.b = static_cast<std::uint8_t>(std::clamp(b_edit->text().toInt(), 0, 255));
    c.a = static_cast<std::uint8_t>(std::clamp(a_edit->text().toInt(), 0, 255));
    picker->set_color(c, true);
  };
  for (auto *edit : {r_edit, g_edit, b_edit, a_edit}) {
    QObject::connect(edit, &QLineEdit::editingFinished, body, commit_channels);
  }

  // Recent colours
  auto *recent_label = new QLabel(QObject::tr("Recent"), body);
  recent_label->setProperty("muted", true);
  layout->addWidget(recent_label);
  auto *recent_row = new QHBoxLayout();
  recent_row->setSpacing(8);
  std::vector<QPushButton *> recent_buttons;
  for (int i = 0; i < 8; ++i) {
    auto *btn = make_swatch(body, core::Rgba8{0, 0, 0, 0});
    btn->setEnabled(false);
    recent_row->addWidget(btn);
    recent_buttons.push_back(btn);
  }
  recent_row->addStretch(1);
  layout->addLayout(recent_row);

  auto refresh_recent = [ctx, recent_buttons, picker, sync_readouts]() {
    if (!ctx.history) return;
    const auto &colors = ctx.history->colors();
    for (std::size_t i = 0; i < recent_buttons.size(); ++i) {
      auto *btn = recent_buttons[i];
      if (i < colors.size()) {
        const auto c = colors[i];
        btn->setEnabled(true);
        btn->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4);"
                                   " border: 1px solid rgba(255,255,255,0.25); border-radius: 10px;")
                               .arg(c.r).arg(c.g).arg(c.b).arg(c.a));
        QObject::disconnect(btn, nullptr, nullptr, nullptr);
        QObject::connect(btn, &QPushButton::clicked, btn, [picker, sync_readouts, c]() {
          picker->set_color(c, true);
          sync_readouts();
        });
      } else {
        btn->setEnabled(false);
        btn->setStyleSheet("background-color: transparent; border: 1px dashed rgba(255,255,255,0.2);"
                           " border-radius: 10px;");
      }
    }
  };
  refresh_recent();

  // Palette profile
  auto *profile_row = new QHBoxLayout();
  auto *profile_label = new QLabel(QObject::tr("Palette"), body);
  profile_label->setProperty("muted", true);
  auto *profile_combo = new QComboBox(body);
  for (const auto &profile : ColorHistory::palette_profiles()) {
    profile_combo->addItem(QString::fromStdString(profile.name));
  }
  if (ctx.history) {
    profile_combo->setCurrentIndex(static_cast<int>(ctx.history->active_profile_index()));
  }
  profile_row->addWidget(profile_label);
  profile_row->addWidget(profile_combo, 1);
  layout->addLayout(profile_row);

  auto *palette_row = new QHBoxLayout();
  palette_row->setSpacing(8);
  std::vector<QPushButton *> palette_buttons;
  for (int i = 0; i < 8; ++i) {
    auto *btn = make_swatch(body, core::Rgba8{0, 0, 0, 0});
    palette_row->addWidget(btn);
    palette_buttons.push_back(btn);
  }
  palette_row->addStretch(1);
  layout->addLayout(palette_row);

  auto refresh_palette = [ctx, profile_combo, palette_buttons, picker, sync_readouts]() {
    const auto &profiles = ColorHistory::palette_profiles();
    if (profiles.empty()) return;
    const std::size_t idx = static_cast<std::size_t>(std::max(0, profile_combo->currentIndex()));
    const auto &colors = profiles[std::min(idx, profiles.size() - 1)].colors;
    for (std::size_t i = 0; i < palette_buttons.size(); ++i) {
      auto *btn = palette_buttons[i];
      if (i < colors.size()) {
        const auto c = colors[i];
        btn->setEnabled(true);
        btn->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4);"
                                   " border: 1px solid rgba(255,255,255,0.25); border-radius: 10px;")
                               .arg(c.r).arg(c.g).arg(c.b).arg(c.a));
        QObject::disconnect(btn, nullptr, nullptr, nullptr);
        QObject::connect(btn, &QPushButton::clicked, btn, [picker, sync_readouts, c]() {
          picker->set_color(c, true);
          sync_readouts();
        });
      } else {
        btn->setEnabled(false);
      }
    }
  };
  QObject::connect(profile_combo, &QComboBox::currentIndexChanged, body, [ctx, refresh_palette](int idx) {
    if (ctx.history) ctx.history->set_active_profile_index(static_cast<std::size_t>(std::max(0, idx)));
    refresh_palette();
  });
  refresh_palette();

  auto *clear_row = new QHBoxLayout();
  auto *clear_btn = new QPushButton(QObject::tr("Clear History"), body);
  clear_btn->setProperty("ghost", true);
  clear_btn->setMinimumHeight(44);
  clear_row->addWidget(clear_btn);
  clear_row->addStretch(1);
  layout->addLayout(clear_row);
  QObject::connect(clear_btn, &QPushButton::clicked, body, [ctx, refresh_recent]() {
    if (ctx.history) ctx.history->clear();
    refresh_recent();
  });

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);

  sheet->set_secondary_action(QObject::tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(QObject::tr("Apply"), [sheet, picker, ctx, refresh_recent]() {
    const core::Rgba8 color = picker->color();
    if (ctx.apply_color) ctx.apply_color(color, true);
    if (ctx.history) ctx.history->record(color);
    refresh_recent();
    sheet->close();
  });

  QObject::connect(picker, &MobileColorPicker::colorChanged, body,
                   [ctx, refresh_recent](const core::Rgba8 &color) {
                     if (ctx.apply_color) ctx.apply_color(color, false);
                     (void)refresh_recent;
                   });
}

} // namespace kreart::ui::mobile
