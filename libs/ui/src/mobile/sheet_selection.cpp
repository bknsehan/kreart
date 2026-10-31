#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <vector>

#include "kreart/core/selection_mask.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

QPushButton *make_action(QWidget *parent, const QString &label, std::function<void()> fn) {
  auto *btn = new QPushButton(label, parent);
  btn->setMinimumHeight(52);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setStyleSheet(
      "QPushButton { background: #1E2127; color: #ECEEF3; border: none; border-radius: 12px;"
      " padding: 0 16px; font-size: 14px; text-align: left; }"
      "QPushButton:pressed { background: #2A2E36; }");
  if (fn) {
    QObject::connect(btn, &QPushButton::clicked, parent, [fn]() { fn(); });
  }
  return btn;
}

} // namespace

void build_selection_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  // Scrollable body: this sheet has more rows than fit a capped sheet height,
  // and a plain layout would compress the 52 px touch targets into each other.
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  Canvas *canvas = ctx.canvas;
  core::Document *doc = ctx.doc;

  auto refresh_canvas = [canvas]() {
    if (canvas) canvas->refresh();
  };

  layout->addWidget(make_action(body, QObject::tr("Select All"), [doc, canvas]() {
    if (!doc) return;
    doc->begin_change();
    doc->selection().set_rect(0, 0, doc->width(), doc->height());
    doc->commit_change();
    if (canvas) {
      canvas->sync_selection_mask_from_image();
      canvas->refresh();
    }
  }));

  layout->addWidget(make_action(body, QObject::tr("Deselect"), [doc, canvas]() {
    if (!doc) return;
    doc->begin_change();
    doc->selection().clear();
    doc->commit_change();
    if (canvas) {
      canvas->sync_selection_mask_from_image();
      canvas->refresh();
    }
  }));

  layout->addWidget(make_action(body, QObject::tr("Invert"), [doc, canvas]() {
    if (!doc) return;
    doc->begin_change();
    doc->selection().invert();
    doc->commit_change();
    if (canvas) {
      canvas->sync_selection_mask_from_image();
      canvas->refresh();
    }
  }));

  layout->addWidget(make_action(body, QObject::tr("Clear Pixels"), [doc, canvas]() {
    if (!doc) return;
    doc->begin_change();
    doc->clear_selection_content();
    doc->commit_change();
    if (canvas) canvas->refresh();
  }));

  layout->addWidget(make_action(body, QObject::tr("Commit Move"), [doc, canvas]() {
    if (!doc) return;
    if (doc->has_floating_selection()) {
      doc->begin_change();
      doc->commit_move_selection();
      doc->commit_change();
      if (canvas) canvas->refresh();
    }
  }));

  // Combine mode
  auto *mode_label = new QLabel(QObject::tr("Combine mode"), body);
  mode_label->setProperty("muted", true);
  layout->addWidget(mode_label);

  auto *mode_host = new QWidget(body);
  auto *mode_row = new QHBoxLayout(mode_host);
  mode_row->setContentsMargins(0, 0, 0, 0);
  mode_row->setSpacing(6);
  const QStringList modes = {QObject::tr("New"), QObject::tr("Add"), QObject::tr("Subtract"),
                             QObject::tr("Intersect")};
  std::vector<QPushButton *> mode_buttons;
  for (const auto &label : modes) {
    auto *btn = new QPushButton(label, mode_host);
    btn->setCheckable(true);
    btn->setMinimumHeight(44);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QPushButton { background: #1E2127; color: #9AA0AD; border: none; border-radius: 12px;"
        " padding: 0 10px; font-size: 13px; }"
        "QPushButton:checked { background: #E8B44A; color: #1A1408; font-weight: 600; }");
    mode_row->addWidget(btn, 1);
    mode_buttons.push_back(btn);
  }
  for (int i = 0; i < static_cast<int>(mode_buttons.size()); ++i) {
    QObject::connect(mode_buttons[i], &QPushButton::clicked, mode_host,
                     [mode_buttons, i, canvas](bool) {
                       for (int j = 0; j < static_cast<int>(mode_buttons.size()); ++j) {
                         mode_buttons[j]->setChecked(j == i);
                       }
                       if (!canvas) return;
                       core::SelectionCombineMode mode = core::SelectionCombineMode::REPLACE;
                       if (i == 1) mode = core::SelectionCombineMode::ADD;
                       else if (i == 2) mode = core::SelectionCombineMode::SUBTRACT;
                       else if (i == 3) mode = core::SelectionCombineMode::INTERSECT;
                       canvas->set_selection_combine_mode(mode);
                     });
  }
  int initial_mode = 0;
  if (canvas) {
    switch (canvas->selection_combine_mode()) {
      case core::SelectionCombineMode::REPLACE: initial_mode = 0; break;
      case core::SelectionCombineMode::ADD: initial_mode = 1; break;
      case core::SelectionCombineMode::SUBTRACT: initial_mode = 2; break;
      case core::SelectionCombineMode::INTERSECT: initial_mode = 3; break;
    }
  }
  if (initial_mode < static_cast<int>(mode_buttons.size())) {
    mode_buttons[initial_mode]->setChecked(true);
  }
  layout->addWidget(mode_host);

  // Numeric selection operations
  auto *ops_label = new QLabel(QObject::tr("Modify"), body);
  ops_label->setProperty("muted", true);
  layout->addWidget(ops_label);

  auto add_numeric = [body, layout, &sheets](const QString &label, double min_value,
                                            double max_value, double initial,
                                            const QString &suffix,
                                            std::function<void(double)> apply) {
    auto *btn = make_action(body, label, nullptr);
    QObject::connect(btn, &QPushButton::clicked, body,
                     [&sheets, label, min_value, max_value, initial, suffix, apply]() {
                       auto *value_sheet = sheets.make_value_sheet(
                           label, QObject::tr("Amount"), min_value, max_value, initial, suffix,
                           apply);
                       sheets.show_sheet_public(value_sheet);
                     });
    layout->addWidget(btn);
  };

  add_numeric(QObject::tr("Feather"), 0.1, 100.0, 4.0, " px", [doc, refresh_canvas](double v) {
    if (!doc) return;
    doc->feather_selection(v);
    refresh_canvas();
  });
  add_numeric(QObject::tr("Grow"), 1.0, 100.0, 5.0, " px", [doc, refresh_canvas](double v) {
    if (!doc) return;
    doc->grow_selection(static_cast<int>(std::lround(v)));
    refresh_canvas();
  });
  add_numeric(QObject::tr("Shrink"), 1.0, 100.0, 5.0, " px", [doc, refresh_canvas](double v) {
    if (!doc) return;
    doc->shrink_selection(static_cast<int>(std::lround(v)));
    refresh_canvas();
  });
  add_numeric(QObject::tr("Border"), 1.0, 100.0, 5.0, " px", [doc, refresh_canvas](double v) {
    if (!doc) return;
    doc->border_selection(static_cast<int>(std::lround(v)));
    refresh_canvas();
  });

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);
}

} // namespace kreart::ui::mobile
