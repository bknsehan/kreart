#include "kreart/ui/mobile/sheets.h"

#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/timelapse_dialog.h"

namespace kreart::ui::mobile {
namespace {

struct SheetSpec {
  const char *id;
  const char *title;
  bool full_height;
};

const SheetSpec kSheetSpecs[] = {
    {"hub", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Studio"), true},
    {"color", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Color"), false},
    {"brushes", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Brushes"), false},
    {"layers", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Layers"), true},
    {"tool", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Tool Settings"), false},
    {"selection", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Selection"), false},
    {"image", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Image"), false},
    {"filters", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Filters"), false},
    {"view", QT_TRANSLATE_NOOP("kreart::ui::mobile", "View"), false},
    {"document", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Document"), false},
    {"timelapse", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Timelapse"), false},
    {"settings", QT_TRANSLATE_NOOP("kreart::ui::mobile", "Settings"), false},
};

} // namespace

EditorSheets::EditorSheets(QWidget *host, EditorContext ctx, QObject *parent)
    : QObject(parent), host_(host), ctx_(std::move(ctx)) {}

MobileSheet *EditorSheets::build(const QString &id) {
  QString title = id;
  bool full = false;
  for (const auto &spec : kSheetSpecs) {
    if (id == QLatin1String(spec.id)) {
      title = QCoreApplication::translate("kreart::ui::mobile", spec.title);
      full = spec.full_height;
      break;
    }
  }

  auto *sheet = new MobileSheet(host_, title, true);
  sheet->set_full_height(full);

  if (id == "hub") build_hub_sheet(sheet, ctx_, *this);
  else if (id == "color") build_color_sheet(sheet, ctx_, *this);
  else if (id == "brushes") build_brush_sheet(sheet, ctx_, *this);
  else if (id == "layers") build_layer_sheet(sheet, ctx_, *this);
  else if (id == "tool") build_tool_sheet(sheet, ctx_, *this);
  else if (id == "selection") build_selection_sheet(sheet, ctx_, *this);
  else if (id == "image") build_image_sheet(sheet, ctx_, *this);
  else if (id == "filters") build_filter_sheet(sheet, ctx_, *this);
  else if (id == "view") build_view_sheet(sheet, ctx_, *this);
  else if (id == "document") build_document_sheet(sheet, ctx_, *this);
  else if (id == "timelapse") {
    if (ctx_.canvas) {
      ctx_.canvas->timelapse_recorder().flush_pending_frames();
      auto *dlg = new TimelapseDialog(ctx_.canvas->timelapse_recorder(), ctx_.canvas->tracker(), host_);
      dlg->setAttribute(Qt::WA_DeleteOnClose);
      dlg->show();
    }
  }
  else if (id == "settings") build_settings_sheet(sheet, ctx_, *this);
  bind(sheet);
  sheets_.insert(id, sheet);
  return sheet;
}

void EditorSheets::bind(MobileSheet *sheet) {
  QObject::connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_ == sheet) active_ = nullptr;
  });
}

void EditorSheets::show_sheet(MobileSheet *sheet) {
  if (!sheet) return;
  if (active_ && active_ != sheet) {
    active_->close();
  }
  active_ = sheet;
  sheet->open();
}

MobileSheet *EditorSheets::show(const QString &id) {
  if (id.isEmpty()) {
    close_active();
    return nullptr;
  }
  MobileSheet *sheet = sheets_.value(id, nullptr);
  if (!sheet) {
    sheet = build(id);
  }
  show_sheet(sheet);
  if (id == "layers") {
    notify_layer_sheet_opened(sheet);
  }
  return sheet;
}

bool EditorSheets::close_active() {
  if (!active_) return false;
  active_->close();
  active_ = nullptr;
  return true;
}

void EditorSheets::relayout() {
  if (active_) active_->relayout();
}

void EditorSheets::register_transient(MobileSheet *sheet) {
  if (!sheet) return;
  QObject::connect(sheet, &MobileSheet::closed, this, [this, sheet]() {
    if (active_ == sheet) active_ = nullptr;
    sheet->deleteLater();
  });
}

MobileSheet *EditorSheets::make_value_sheet(const QString &title, const QString &label,
                                            double min_value, double max_value, double initial,
                                            const QString &suffix,
                                            std::function<void(double)> on_apply) {
  auto *sheet = new MobileSheet(host_, title, true);

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto *value_label = new QLabel(body);
  value_label->setFont(font_title());
  auto format_value = [suffix](double v) {
    const QString num = (std::abs(v - std::round(v)) < 0.05)
                            ? QString::number(static_cast<int>(std::round(v)))
                            : QString::number(v, 'f', 1);
    return num + suffix;
  };
  value_label->setText(format_value(initial));

  auto *name_label = new QLabel(label, body);
  name_label->setProperty("muted", true);

  auto *slider = new QSlider(Qt::Horizontal, body);
  slider->setMinimumHeight(44);
  // Sliders are integral, so fractional ranges are scaled by 10.
  const bool fractional = (max_value - min_value) <= 200.0 &&
                          (std::abs(min_value - std::round(min_value)) > 1e-6 ||
                           std::abs(max_value - std::round(max_value)) > 1e-6);
  const double scale = fractional ? 10.0 : 1.0;
  slider->setRange(static_cast<int>(std::lround(min_value * scale)),
                   static_cast<int>(std::lround(max_value * scale)));
  slider->setValue(static_cast<int>(std::lround(initial * scale)));

  QObject::connect(slider, &QSlider::valueChanged, body, [value_label, format_value, scale](int v) {
    value_label->setText(format_value(v / scale));
  });

  layout->addWidget(value_label);
  layout->addWidget(name_label);
  layout->addWidget(slider);
  sheet->set_body(body);

  sheet->set_secondary_action(QObject::tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(QObject::tr("Apply"), [sheet, slider, scale, on_apply]() {
    const double value = slider->value() / scale;
    sheet->close();
    if (on_apply) on_apply(value);
  });

  register_transient(sheet);
  return sheet;
}

MobileSheet *EditorSheets::make_text_sheet(const QString &title, const QString &label,
                                           const QString &initial,
                                           std::function<bool(const QString &, QString &)> on_accept) {
  auto *sheet = new MobileSheet(host_, title, true);

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto *name_label = new QLabel(label, body);
  name_label->setProperty("muted", true);
  auto *edit = new QLineEdit(body);
  edit->setText(initial);
  edit->setMinimumHeight(44);
  edit->selectAll();
  auto *error_label = new QLabel(body);
  error_label->setStyleSheet("color: #F45C5C; font-size: 12px;");
  error_label->hide();

  layout->addWidget(name_label);
  layout->addWidget(edit);
  layout->addWidget(error_label);
  sheet->set_body(body);

  auto submit = [sheet, edit, error_label, on_accept]() {
    QString error;
    const QString text = edit->text().trimmed();
    if (on_accept && on_accept(text, error)) {
      sheet->close();
    } else {
      error_label->setText(error.isEmpty() ? QObject::tr("Invalid value") : error);
      error_label->show();
    }
  };
  QObject::connect(edit, &QLineEdit::returnPressed, body, submit);

  sheet->set_secondary_action(QObject::tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(QObject::tr("Save"), submit);

  register_transient(sheet);
  return sheet;
}

MobileSheet *EditorSheets::make_confirm_sheet(const QString &title, const QString &message,
                                              const QString &confirm_label, bool danger,
                                              std::function<void()> on_confirm) {
  auto *sheet = new MobileSheet(host_, title, true);

  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto *msg = new QLabel(message, body);
  msg->setWordWrap(true);
  msg->setFont(font_body());
  layout->addWidget(msg);
  sheet->set_body(body);

  sheet->set_secondary_action(QObject::tr("Cancel"), [sheet]() { sheet->close(); });
  sheet->set_primary_action(confirm_label, [sheet, on_confirm]() {
    sheet->close();
    if (on_confirm) on_confirm();
  });
  if (danger && sheet->primary_button()) {
    sheet->primary_button()->setProperty("accent", false);
    sheet->primary_button()->setProperty("danger", true);
    sheet->primary_button()->style()->unpolish(sheet->primary_button());
    sheet->primary_button()->style()->polish(sheet->primary_button());
  }

  register_transient(sheet);
  return sheet;
}

} // namespace kreart::ui::mobile
