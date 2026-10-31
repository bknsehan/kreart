#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "kreart/core/filters.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

// Live-preview filter transaction. Mirrors BaseFilterDialog semantics without a
// dialog: snapshot the active layer, begin a transaction, restore + re-apply on
// every parameter change, commit on Apply, roll back on any other close.
class FilterSession : public QObject {
public:
  FilterSession(EditorContext &ctx, QObject *parent) : QObject(parent), ctx_(ctx) {
    if (ctx_.doc && ctx_.doc->has_active_layer() && !ctx_.doc->active_layer().is_group() &&
        !ctx_.doc->active_layer().locked()) {
      original_ = ctx_.doc->active_layer().image();
      ctx_.doc->begin_change();
      valid_ = true;
    }
    debounce_.setSingleShot(true);
    debounce_.setInterval(16);
    connect(&debounce_, &QTimer::timeout, this, [this]() { run_preview(); });
  }

  ~FilterSession() override {
    if (valid_ && !committed_) {
      rollback();
    }
  }

  bool valid() const { return valid_; }
  core::Document *doc() const { return ctx_.doc; }

  void set_apply(std::function<void()> fn) { apply_ = std::move(fn); }

  void schedule_preview() {
    if (valid_) debounce_.start();
  }

  void run_preview() {
    if (!valid_ || !ctx_.doc || !ctx_.doc->has_active_layer()) return;
    if (ctx_.doc->active_layer().is_group()) return;
    ctx_.doc->active_layer().image() = original_;
    if (apply_) apply_();
    if (ctx_.canvas) ctx_.canvas->refresh();
  }

  void commit() {
    if (!valid_ || committed_) return;
    committed_ = true;
    debounce_.stop();
    if (ctx_.doc) {
      ctx_.doc->commit_change();
      ctx_.doc->mark_composite_dirty();
    }
    if (ctx_.canvas) ctx_.canvas->refresh();
  }

  void rollback() {
    if (!valid_) return;
    debounce_.stop();
    if (ctx_.doc && ctx_.doc->has_active_layer()) {
      ctx_.doc->active_layer().image() = original_;
      ctx_.doc->cancel_change();
      ctx_.doc->mark_composite_dirty();
    }
    if (ctx_.canvas) ctx_.canvas->refresh();
    valid_ = false;
  }

private:
  EditorContext &ctx_;
  core::TiledImage original_{0, 0};
  std::function<void()> apply_;
  bool valid_ = false;
  bool committed_ = false;
  QTimer debounce_;
};

QWidget *make_slider_block(QWidget *parent, const QString &label, int min_value, int max_value,
                           int value, const QString &suffix, std::function<void(int)> on_change) {
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

QWidget *make_entry_button(QWidget *parent, const QString &label, std::function<void()> fn) {
  auto *btn = new QPushButton(label, parent);
  btn->setMinimumHeight(52);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setStyleSheet(
      "QPushButton { background: #1E2127; color: #ECEEF3; border: none; border-radius: 12px;"
      " padding: 0 16px; font-size: 14px; text-align: left; }"
      "QPushButton:pressed { background: #2A2E36; }");
  QObject::connect(btn, &QPushButton::clicked, parent, [fn]() { if (fn) fn(); });
  return btn;
}

} // namespace

void build_filter_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  auto *scroll = new QScrollArea(sheet);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *body = new QWidget(scroll);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  core::Document *doc = ctx.doc;
  auto can_filter = [doc]() {
    return doc && doc->has_active_layer() && !doc->active_layer().is_group() &&
           !doc->active_layer().locked();
  };

  // Builds a filter sheet whose content is produced by `build`. Apply/Cancel are
  // wired to the FilterSession transaction; any other close rolls back.
  auto open_filter = [&sheets, &ctx, can_filter](
                         const QString &title,
                         std::function<void(FilterSession &, QWidget *, QVBoxLayout *)> build) {
    if (!can_filter()) {
      if (ctx.hud) ctx.hud(QObject::tr("Select an unlocked paint layer first"));
      return;
    }
    auto *filter_sheet = new MobileSheet(sheets.host(), title, true);
    auto *session = new FilterSession(ctx, filter_sheet);
    if (!session->valid()) {
      filter_sheet->deleteLater();
      if (ctx.hud) ctx.hud(QObject::tr("Select an unlocked paint layer first"));
      return;
    }

    auto *page_scroll = new QScrollArea(filter_sheet);
    page_scroll->setWidgetResizable(true);
    page_scroll->setFrameShape(QFrame::NoFrame);
    auto *page_body = new QWidget(page_scroll);
    auto *page_layout = new QVBoxLayout(page_body);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->setSpacing(10);

    build(*session, page_body, page_layout);

    page_layout->addStretch(1);
    page_scroll->setWidget(page_body);
    filter_sheet->set_body(page_scroll);

    filter_sheet->set_secondary_action(QObject::tr("Cancel"),
                                       [filter_sheet]() { filter_sheet->close(); });
    filter_sheet->set_primary_action(QObject::tr("Apply"), [filter_sheet, session]() {
      session->commit();
      filter_sheet->close();
    });

    QObject::connect(filter_sheet, &MobileSheet::closed, filter_sheet,
                     [session]() { session->rollback(); });

    sheets.register_transient(filter_sheet);
    sheets.show_sheet_public(filter_sheet);
  };

  // ---- Hue / Saturation / Value -----------------------------------------
  layout->addWidget(make_entry_button(body, QObject::tr("Hue / Saturation / Value"), [open_filter]() {
    open_filter(QObject::tr("Hue / Saturation / Value"),
                [](FilterSession &session, QWidget *parent, QVBoxLayout *layout) {
                  auto state = std::make_shared<std::array<int, 3>>(std::array<int, 3>{0, 0, 0});
                  session.set_apply([&session, state]() {
                    core::Document *doc = session.doc();
                    if (!doc) return;
                    core::filter_hsv(doc->active_layer().image(), (*state)[0], (*state)[1] / 100.0,
                                     (*state)[2] / 100.0,
                                     doc->selection().active() ? &doc->selection() : nullptr);
                  });
                  layout->addWidget(make_slider_block(
                      parent, QObject::tr("Hue"), -180, 180, 0, QStringLiteral("\u00b0"),
                      [&session, state](int v) { (*state)[0] = v; session.schedule_preview(); }));
                  layout->addWidget(make_slider_block(
                      parent, QObject::tr("Saturation"), -100, 100, 0, "%",
                      [&session, state](int v) { (*state)[1] = v; session.schedule_preview(); }));
                  layout->addWidget(make_slider_block(
                      parent, QObject::tr("Value"), -100, 100, 0, "%",
                      [&session, state](int v) { (*state)[2] = v; session.schedule_preview(); }));
                });
  }));

  // ---- Brightness / Contrast -------------------------------------------
  layout->addWidget(make_entry_button(body, QObject::tr("Brightness / Contrast"), [open_filter]() {
    open_filter(QObject::tr("Brightness / Contrast"),
                [](FilterSession &session, QWidget *parent, QVBoxLayout *layout) {
                  auto state = std::make_shared<std::array<int, 2>>(std::array<int, 2>{0, 0});
                  session.set_apply([&session, state]() {
                    core::Document *doc = session.doc();
                    if (!doc) return;
                    core::filter_brightness_contrast(
                        doc->active_layer().image(), (*state)[0], (*state)[1],
                        doc->selection().active() ? &doc->selection() : nullptr);
                  });
                  layout->addWidget(make_slider_block(
                      parent, QObject::tr("Brightness"), -100, 100, 0, "",
                      [&session, state](int v) { (*state)[0] = v; session.schedule_preview(); }));
                  layout->addWidget(make_slider_block(
                      parent, QObject::tr("Contrast"), -100, 100, 0, "",
                      [&session, state](int v) { (*state)[1] = v; session.schedule_preview(); }));
                });
  }));

  // ---- Color Balance ---------------------------------------------------
  layout->addWidget(make_entry_button(body, QObject::tr("Color Balance"), [open_filter]() {
    open_filter(QObject::tr("Color Balance"),
                [](FilterSession &session, QWidget *parent, QVBoxLayout *layout) {
                  auto params = std::make_shared<core::ColorBalanceParams>();
                  auto tone = std::make_shared<int>(1);
                  session.set_apply([&session, params]() {
                    core::Document *doc = session.doc();
                    if (!doc) return;
                    core::filter_color_balance(
                        doc->active_layer().image(), *params,
                        doc->selection().active() ? &doc->selection() : nullptr);
                  });

                  auto tone_of = [params](int t) -> core::ColorBalanceTones & {
                    if (t == 0) return params->shadows;
                    if (t == 1) return params->midtones;
                    return params->highlights;
                  };

                  // Tone chips
                  auto *tone_host = new QWidget(parent);
                  auto *tone_row = new QHBoxLayout(tone_host);
                  tone_row->setContentsMargins(0, 0, 0, 0);
                  tone_row->setSpacing(6);
                  const QStringList tones = {QObject::tr("Shadows"), QObject::tr("Midtones"),
                                             QObject::tr("Highlights")};
                  std::vector<QPushButton *> tone_buttons;
                  for (const auto &label : tones) {
                    auto *btn = new QPushButton(label, tone_host);
                    btn->setCheckable(true);
                    btn->setMinimumHeight(44);
                    btn->setCursor(Qt::PointingHandCursor);
                    btn->setStyleSheet(
                        "QPushButton { background: #1E2127; color: #9AA0AD; border: none;"
                        " border-radius: 12px; padding: 0 10px; font-size: 13px; }"
                        "QPushButton:checked { background: #E8B44A; color: #1A1408;"
                        " font-weight: 600; }");
                    tone_row->addWidget(btn, 1);
                    tone_buttons.push_back(btn);
                  }
                  layout->addWidget(tone_host);

                  // One slider block per channel. Each block owns its slider and
                  // writes into the currently selected tone.
                  auto add_channel = [&](const QString &label,
                                         double core::ColorBalanceTones::*field) {
                    auto *host = new QWidget(parent);
                    auto *host_layout = new QVBoxLayout(host);
                    host_layout->setContentsMargins(0, 0, 0, 0);
                    host_layout->setSpacing(2);
                    auto *header = new QHBoxLayout();
                    auto *name = new QLabel(label, host);
                    name->setProperty("muted", true);
                    auto *readout = new QLabel(host);
                    readout->setFont(font_body());
                    header->addWidget(name);
                    header->addStretch(1);
                    header->addWidget(readout);
                    auto *slider = new QSlider(Qt::Horizontal, host);
                    slider->setRange(-100, 100);
                    slider->setMinimumHeight(44);
                    host_layout->addLayout(header);
                    host_layout->addWidget(slider);
                    layout->addWidget(host);

                    auto sync_readout = [readout, slider]() {
                      readout->setText(QString::number(slider->value()));
                    };
                    QObject::connect(slider, &QSlider::valueChanged, host,
                                     [&session, tone_of, tone, field, sync_readout](int v) {
                                       tone_of(*tone).*field = v;
                                       sync_readout();
                                       session.schedule_preview();
                                     });
                    return slider;
                  };
                  auto *cr = add_channel(QObject::tr("Cyan / Red"),
                                         &core::ColorBalanceTones::cyan_red);
                  auto *mg = add_channel(QObject::tr("Magenta / Green"),
                                         &core::ColorBalanceTones::magenta_green);
                  auto *yb = add_channel(QObject::tr("Yellow / Blue"),
                                         &core::ColorBalanceTones::yellow_blue);

                  auto load_tone = [params, tone_of, cr, mg, yb](int t) {
                    const auto &tones_ref = tone_of(t);
                    QSignalBlocker b1(cr), b2(mg), b3(yb);
                    cr->setValue(static_cast<int>(tones_ref.cyan_red));
                    mg->setValue(static_cast<int>(tones_ref.magenta_green));
                    yb->setValue(static_cast<int>(tones_ref.yellow_blue));
                  };
                  for (int i = 0; i < static_cast<int>(tone_buttons.size()); ++i) {
                    QObject::connect(tone_buttons[i], &QPushButton::clicked, tone_host,
                                     [tone_buttons, i, tone, load_tone](bool) {
                                       for (int j = 0; j < static_cast<int>(tone_buttons.size()); ++j) {
                                         tone_buttons[j]->setChecked(j == i);
                                       }
                                       *tone = i;
                                       load_tone(i);
                                     });
                  }
                  tone_buttons[1]->setChecked(true);
                  load_tone(1);

                  auto *preserve = new QCheckBox(QObject::tr("Preserve Luminosity"), parent);
                  preserve->setChecked(true);
                  QObject::connect(preserve, &QCheckBox::toggled, parent,
                                   [params, &session](bool on) {
                                     params->preserve_luminosity = on;
                                     session.schedule_preview();
                                   });
                  layout->addWidget(preserve);
                });
  }));

  // ---- Gaussian Blur ---------------------------------------------------
  layout->addWidget(make_entry_button(body, QObject::tr("Gaussian Blur"), [open_filter]() {
    open_filter(QObject::tr("Gaussian Blur"),
                [](FilterSession &session, QWidget *parent, QVBoxLayout *layout) {
                  auto radius = std::make_shared<int>(50);
                  session.set_apply([&session, radius]() {
                    core::Document *doc = session.doc();
                    if (!doc) return;
                    core::filter_gaussian_blur(
                        doc->active_layer().image(), *radius / 10.0,
                        doc->selection().active() ? &doc->selection() : nullptr);
                  });
                  layout->addWidget(make_slider_block(
                      parent, QObject::tr("Radius"), 5, 500, *radius, " px",
                      [&session, radius](int v) {
                        *radius = v;
                        session.schedule_preview();
                      }));
                });
  }));

  layout->addStretch(1);
  scroll->setWidget(body);
  sheet->set_body(scroll);
}

} // namespace kreart::ui::mobile
