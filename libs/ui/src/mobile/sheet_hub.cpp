#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

struct TileSpec {
  const char *label;
  const char *icon;
  const char *sheet_id;
};

const TileSpec kTiles[] = {
    {"Color", ":/icons/icons/tool-picker-symbolic.svg", "color"},
    {"Brushes", ":/icons/icons/tool-brush-symbolic.svg", "brushes"},
    {"Layers", ":/icons/icons/layer-symbolic.svg", "layers"},
    {"Tool Settings", ":/icons/icons/tool-smudge-symbolic.svg", "tool"},
    {"Selection", ":/icons/icons/tool-rect-select-symbolic.svg", "selection"},
    {"Image", ":/icons/icons/view-grid-symbolic.svg", "image"},
    {"Filters", ":/icons/icons/tool-fill-symbolic.svg", "filters"},
    {"View", ":/icons/icons/view-list-symbolic.svg", "view"},
    {"Document", ":/icons/icons/folder-symbolic.svg", "document"},
    {"Timelapse", ":/icons/icons/view-list-symbolic.svg", "timelapse"},
    {"Settings", ":/icons/icons/emblem-system-symbolic.svg", "settings"},
};

} // namespace

void build_hub_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  (void)ctx;
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  auto *body = new QWidget(scroll);
  auto *layout = new QGridLayout(body);
  layout->setContentsMargins(0, 4, 0, 4);
  layout->setHorizontalSpacing(tokens().gap);
  layout->setVerticalSpacing(tokens().gap);

  int index = 0;
  for (const auto &tile : kTiles) {
    auto *btn = new QPushButton(QString::fromUtf8(tile.label), body);
    btn->setProperty("tile", true);
    btn->setMinimumHeight(72);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIcon(IconHelper::load_symbolic(QString::fromUtf8(tile.icon), tokens().text, 24));
    btn->setIconSize(QSize(24, 24));
    const QString sheet_id = QString::fromUtf8(tile.sheet_id);
    QObject::connect(btn, &QPushButton::clicked, btn, [&sheets, sheet_id]() {
      sheets.show(sheet_id);
    });
    layout->addWidget(btn, index / 2, index % 2);
    ++index;
  }
  layout->setRowStretch(index / 2, 1);

  scroll->setWidget(body);
  sheet->set_body(scroll);
}

} // namespace kreart::ui::mobile
