#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

#include "kreart/core/tiled_image.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

struct AnchorSpec {
  const char *label;
  int index;
};

const AnchorSpec kAnchors[] = {{"Center", 0},      {"Top-Left", 1},    {"Top-Right", 2},
                               {"Bottom-Left", 3}, {"Bottom-Right", 4}};

QWidget *make_dimension_row(QWidget *parent, const QString &label, QLineEdit **out_edit,
                            int value, int min_value, int max_value) {
  auto *row = new QWidget(parent);
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto *name = new QLabel(label, row);
  name->setProperty("muted", true);
  auto *edit = new QLineEdit(row);
  edit->setMinimumHeight(44);
  edit->setValidator(new QIntValidator(min_value, max_value, edit));
  edit->setText(QString::number(value));
  layout->addWidget(name);
  layout->addWidget(edit, 1);
  *out_edit = edit;
  return row;
}

} // namespace

void build_image_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  core::Document *doc = ctx.doc;
  Canvas *canvas = ctx.canvas;

  // ---- Canvas Size -------------------------------------------------------
  auto *canvas_title = new QLabel(QObject::tr("Canvas Size"), body);
  canvas_title->setFont(font_section());
  layout->addWidget(canvas_title);

  QLineEdit *cw_edit = nullptr;
  QLineEdit *ch_edit = nullptr;
  layout->addWidget(make_dimension_row(body, QObject::tr("Width"), &cw_edit,
                                       doc ? doc->width() : 1920, 16, 16384));
  layout->addWidget(make_dimension_row(body, QObject::tr("Height"), &ch_edit,
                                       doc ? doc->height() : 1080, 16, 16384));

  auto *anchor_label = new QLabel(QObject::tr("Anchor"), body);
  anchor_label->setProperty("muted", true);
  layout->addWidget(anchor_label);
  auto *anchor_combo = new QComboBox(body);
  for (const auto &anchor : kAnchors) {
    anchor_combo->addItem(QObject::tr(anchor.label));
  }
  layout->addWidget(anchor_combo);

  auto *apply_canvas = new QPushButton(QObject::tr("Apply Canvas Size"), body);
  apply_canvas->setMinimumHeight(48);
  apply_canvas->setProperty("accent", true);
  layout->addWidget(apply_canvas);

  QObject::connect(apply_canvas, &QPushButton::clicked, body,
                   [doc, canvas, cw_edit, ch_edit, anchor_combo, &ctx]() {
                     if (!doc) return;
                     const int new_w = cw_edit->text().toInt();
                     const int new_h = ch_edit->text().toInt();
                     if (new_w < 16 || new_h < 16) return;
                     const int old_w = doc->width();
                     const int old_h = doc->height();
                     int offset_x = (new_w - old_w) / 2;
                     int offset_y = (new_h - old_h) / 2;
                     switch (anchor_combo->currentIndex()) {
                       case 1: offset_x = 0; offset_y = 0; break;
                       case 2: offset_x = new_w - old_w; offset_y = 0; break;
                       case 3: offset_x = 0; offset_y = new_h - old_h; break;
                       case 4: offset_x = new_w - old_w; offset_y = new_h - old_h; break;
                       default: break;
                     }
                     doc->begin_change();
                     doc->resize_canvas(new_w, new_h, offset_x, offset_y);
                     doc->commit_change();
                     if (canvas) {
                       canvas->reposition_symmetry_axes();
                       canvas->refresh();
                       canvas->zoom_fit();
                     }
                     if (ctx.hud) ctx.hud(QObject::tr("Canvas resized"));
                   });

  // ---- Scale Image -------------------------------------------------------
  auto *scale_title = new QLabel(QObject::tr("Scale Image"), body);
  scale_title->setFont(font_section());
  layout->addWidget(scale_title);

  QLineEdit *sw_edit = nullptr;
  QLineEdit *sh_edit = nullptr;
  layout->addWidget(make_dimension_row(body, QObject::tr("Width"), &sw_edit,
                                       doc ? doc->width() : 1920, 16, 16384));
  layout->addWidget(make_dimension_row(body, QObject::tr("Height"), &sh_edit,
                                       doc ? doc->height() : 1080, 16, 16384));

  auto *constrain = new QCheckBox(QObject::tr("Constrain proportions"), body);
  constrain->setChecked(true);
  layout->addWidget(constrain);

  // Keeping the ratio means editing one field updates the other.
  auto sync_ratio = [doc, sw_edit, sh_edit, constrain](bool width_edited) {
    if (!constrain->isChecked() || !doc) return;
    const double aspect = static_cast<double>(doc->width()) / std::max(1, doc->height());
    if (width_edited) {
      const int w = sw_edit->text().toInt();
      sh_edit->setText(QString::number(std::max(16, static_cast<int>(std::lround(w / aspect)))));
    } else {
      const int h = sh_edit->text().toInt();
      sw_edit->setText(QString::number(std::max(16, static_cast<int>(std::lround(h * aspect)))));
    }
  };
  QObject::connect(sw_edit, &QLineEdit::editingFinished, body, [sync_ratio]() { sync_ratio(true); });
  QObject::connect(sh_edit, &QLineEdit::editingFinished, body, [sync_ratio]() { sync_ratio(false); });

  auto *percent_host = new QWidget(body);
  auto *percent_row = new QHBoxLayout(percent_host);
  percent_row->setContentsMargins(0, 0, 0, 0);
  percent_row->setSpacing(6);
  const int percents[] = {25, 50, 200, 400};
  std::vector<QPushButton *> percent_buttons;
  for (int percent : percents) {
    auto *btn = new QPushButton(QString::number(percent) + "%", percent_host);
    btn->setMinimumHeight(44);
    btn->setCursor(Qt::PointingHandCursor);
    QObject::connect(btn, &QPushButton::clicked, percent_host,
                     [doc, sw_edit, sh_edit, percent]() {
                       if (!doc) return;
                       sw_edit->setText(QString::number(std::max(16, doc->width() * percent / 100)));
                       sh_edit->setText(QString::number(std::max(16, doc->height() * percent / 100)));
                     });
    percent_row->addWidget(btn, 1);
    percent_buttons.push_back(btn);
  }
  layout->addWidget(percent_host);

  auto *filter_label = new QLabel(QObject::tr("Filter"), body);
  filter_label->setProperty("muted", true);
  layout->addWidget(filter_label);
  auto *filter_combo = new QComboBox(body);
  filter_combo->addItems({QObject::tr("Nearest"), QObject::tr("Box"), QObject::tr("Bilinear"),
                          QObject::tr("Bicubic"), QObject::tr("Lanczos3")});
  filter_combo->setCurrentIndex(4);
  layout->addWidget(filter_combo);

  auto *apply_scale = new QPushButton(QObject::tr("Apply Scale"), body);
  apply_scale->setMinimumHeight(48);
  apply_scale->setProperty("accent", true);
  layout->addWidget(apply_scale);

  QObject::connect(apply_scale, &QPushButton::clicked, body,
                   [doc, canvas, sw_edit, sh_edit, filter_combo, &ctx]() {
                     if (!doc) return;
                     const int new_w = sw_edit->text().toInt();
                     const int new_h = sh_edit->text().toInt();
                     if (new_w < 16 || new_h < 16) return;
                     core::ScaleFilter filter = core::ScaleFilter::LANCZOS3;
                     switch (filter_combo->currentIndex()) {
                       case 0: filter = core::ScaleFilter::NEAREST; break;
                       case 1: filter = core::ScaleFilter::BOX; break;
                       case 2: filter = core::ScaleFilter::BILINEAR; break;
                       case 3: filter = core::ScaleFilter::BICUBIC; break;
                       default: filter = core::ScaleFilter::LANCZOS3; break;
                     }
                     doc->begin_change();
                     doc->scale_image(new_w, new_h, filter);
                     doc->commit_change();
                     if (canvas) {
                       canvas->reposition_symmetry_axes();
                       canvas->refresh();
                       canvas->zoom_fit();
                     }
                     if (ctx.hud) ctx.hud(QObject::tr("Image scaled"));
                   });

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);
}

} // namespace kreart::ui::mobile
