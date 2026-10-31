#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <memory>
#include <tuple>
#include <vector>

#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

const char *const kCategories[] = {"All", "Erasers", "Sketch", "Ink",
                                   "Digital", "Paint", "Textures", "FX"};

} // namespace

void build_brush_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  (void)sheets;
  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto *search = new QLineEdit(body);
  search->setPlaceholderText(QObject::tr("Search brushes"));
  search->setMinimumHeight(44);
  layout->addWidget(search);

  // Category chips in a horizontally scrollable strip.
  auto *chip_scroll = new QScrollArea(body);
  chip_scroll->setWidgetResizable(true);
  chip_scroll->setFrameShape(QFrame::NoFrame);
  chip_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  chip_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  chip_scroll->setFixedHeight(52);
  auto *chip_host = new QWidget(chip_scroll);
  auto *chip_row = new QHBoxLayout(chip_host);
  chip_row->setContentsMargins(0, 0, 0, 0);
  chip_row->setSpacing(8);
  std::vector<QPushButton *> chips;
  for (const char *category : kCategories) {
    auto *chip = new QPushButton(QString::fromUtf8(category), chip_host);
    chip->setCheckable(true);
    chip->setMinimumHeight(44);
    chip->setMinimumWidth(72);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setStyleSheet(
        "QPushButton { background: #1E2127; color: #ECEEF3; border: none; border-radius: 22px;"
        " padding: 0 16px; font-size: 13px; }"
        "QPushButton:checked { background: #E8B44A; color: #1A1408; font-weight: 600; }");
    chip_row->addWidget(chip);
    chips.push_back(chip);
  }
  chip_row->addStretch(1);
  chip_scroll->setWidget(chip_host);
  layout->addWidget(chip_scroll);
  chips.front()->setChecked(true);

  auto *list = new QListWidget(body);
  list->setUniformItemSizes(false);
  list->setSpacing(4);
  layout->addWidget(list, 1);

  struct FilterState {
    QString category = "All";
    QString search;
  };
  auto filter = std::make_shared<FilterState>();

  auto rebuild = [ctx, list, filter]() {
    list->clear();
    if (!ctx.presets) return;
    const auto &presets = ctx.presets->presets();
    for (std::size_t i = 0; i < presets.size(); ++i) {
      const auto &preset = presets[i];
      if (filter->category != "All" &&
          QString::fromStdString(preset.category) != filter->category) {
        continue;
      }
      const QString name = QString::fromStdString(preset.display_name.empty()
                                                      ? preset.name
                                                      : preset.display_name);
      if (!filter->search.isEmpty() && !name.toLower().contains(filter->search)) {
        continue;
      }

      auto *item = new QListWidgetItem(list);
      item->setSizeHint(QSize(180, 64));
      // Store the DATABASE index, never the filtered row index.
      item->setData(Qt::UserRole, static_cast<qulonglong>(i));

      auto *row = new QWidget(list);
      auto *row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(8, 4, 8, 4);
      row_layout->setSpacing(10);

      auto *preview = new QLabel(row);
      preview->setFixedSize(180, 28);
      preview->setScaledContents(false);
      if (ctx.previews) {
        const QPixmap ribbon = ctx.previews->get_preview(preset, ctx.presets, true, 180, 28);
        if (!ribbon.isNull()) preview->setPixmap(ribbon);
      }
      row_layout->addWidget(preview, 2);

      auto *name_label = new QLabel(name, row);
      name_label->setFont(font_body());
      name_label->setMinimumWidth(110);
      name_label->setMaximumWidth(160);
      row_layout->addWidget(name_label, 1);

      if (preset.is_eraser()) {
        auto *badge = new QLabel(QObject::tr("Eraser"), row);
        badge->setFixedHeight(22);
        badge->setAlignment(Qt::AlignCenter);
        badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        badge->setStyleSheet("color: #1A1408; background: #E8B44A; border-radius: 8px;"
                             " padding: 0 8px; font-size: 11px; font-weight: 600;");
        row_layout->addWidget(badge, 0, Qt::AlignVCenter);
      } else {
        row_layout->addSpacing(0);
      }

      list->setItemWidget(item, row);
    }

    if (list->count() == 0) {
      auto *item = new QListWidgetItem(list);
      item->setSizeHint(QSize(180, 64));
      auto *row = new QWidget(list);
      auto *row_layout = new QHBoxLayout(row);
      auto *label = new QLabel(QObject::tr("No brush presets found - Import a brush pack"), row);
      label->setProperty("muted", true);
      row_layout->addWidget(label);
      list->setItemWidget(item, row);
    }
  };

  QObject::connect(search, &QLineEdit::textChanged, body, [filter, rebuild](const QString &text) {
    filter->search = text.trimmed().toLower();
    rebuild();
  });
  for (auto *chip : chips) {
    QObject::connect(chip, &QPushButton::clicked, body, [chip, chips, filter, rebuild](bool) {
      for (auto *other : chips) other->setChecked(other == chip);
      filter->category = chip->text();
      rebuild();
    });
  }

  QObject::connect(list, &QListWidget::itemClicked, body, [ctx, list](QListWidgetItem *item) {
    if (!ctx.canvas || !ctx.presets) return;
    const auto db_index = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
    const auto &presets = ctx.presets->presets();
    if (db_index >= presets.size()) return;
    const auto &preset = presets[db_index];

    // 1. Apply the preset's brush parameters.
    const core::Rgba8 current = ctx.current_color();
    ctx.canvas->set_brush_settings(
        preset.brush_settings(ctx.canvas->brush_settings(), ctx.presets));

    // 2. Eraser routing. Canvas::set_brush_settings ends with update_brush_mode(),
    //    which derives brush::Mode from the tool, so the tool must change here.
    if (preset.is_eraser()) {
      if (ctx.set_tool) ctx.set_tool(Canvas::Tool::ERASER);
      else ctx.canvas->set_tool(Canvas::Tool::ERASER);
    } else if (ctx.canvas->tool() == Canvas::Tool::ERASER) {
      if (ctx.set_tool) ctx.set_tool(Canvas::Tool::BRUSH);
      else ctx.canvas->set_tool(Canvas::Tool::BRUSH);
    }

    // 3. Preserve the user's colour.
    ctx.canvas->set_brush_color(current);
    if (ctx.hud) ctx.hud(QObject::tr("Brush: %1").arg(QString::fromStdString(preset.name)));
  });

  rebuild();

  // Size and opacity controls
  auto make_slider_row = [body](const QString &label, int min_value, int max_value, int value) {
    auto *row = new QWidget(body);
    auto *row_layout = new QVBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(2);
    auto *header = new QHBoxLayout();
    auto *name = new QLabel(label, row);
    name->setProperty("muted", true);
    auto *readout = new QLabel(row);
    readout->setFont(font_body());
    header->addWidget(name);
    header->addStretch(1);
    header->addWidget(readout);
    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setMinimumHeight(44);
    slider->setRange(min_value, max_value);
    slider->setValue(value);
    row_layout->addLayout(header);
    row_layout->addWidget(slider);
    return std::make_tuple(row, slider, readout);
  };

  auto [size_row, size_slider, size_readout] =
      make_slider_row(QObject::tr("Size"), 1, 500,
                      ctx.canvas ? static_cast<int>(std::lround(ctx.canvas->brush_size())) : 32);
  auto [opacity_row, opacity_slider, opacity_readout] =
      make_slider_row(QObject::tr("Opacity"), 1, 100,
                      ctx.canvas ? static_cast<int>(std::lround(ctx.canvas->brush_settings().opacity * 100.0))
                                 : 100);
  size_readout->setText(QString::number(size_slider->value()) + " px");
  opacity_readout->setText(QString::number(opacity_slider->value()) + "%");

  QObject::connect(size_slider, &QSlider::valueChanged, body, [ctx, size_readout](int v) {
    size_readout->setText(QString::number(v) + " px");
    if (ctx.canvas) ctx.canvas->set_brush_size(v);
  });
  QObject::connect(opacity_slider, &QSlider::valueChanged, body, [ctx, opacity_readout](int v) {
    opacity_readout->setText(QString::number(v) + "%");
    if (ctx.canvas) ctx.canvas->set_brush_opacity(v / 100.0);
  });

  auto *reset_size = new QPushButton(QObject::tr("Reset size"), body);
  reset_size->setProperty("ghost", true);
  reset_size->setMinimumHeight(44);
  QObject::connect(reset_size, &QPushButton::clicked, body, [size_slider]() {
    size_slider->setValue(32);
  });

  layout->addWidget(size_row);
  layout->addWidget(opacity_row);
  auto *reset_row = new QHBoxLayout();
  reset_row->addWidget(reset_size);
  reset_row->addStretch(1);
  layout->addLayout(reset_row);

  sheet->set_body(body);
}

} // namespace kreart::ui::mobile
