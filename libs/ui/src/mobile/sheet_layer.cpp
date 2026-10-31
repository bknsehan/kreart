#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "kreart/core/document.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/layer_panel.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/sheet_builders.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui::mobile {
namespace {

const char *const kBlendModes[] = {"normal", "multiply", "screen", "add", "overlay",
                                   "soft_light", "darken", "lighten", "color_burn", "color_dodge"};
const char *const kBlendLabels[] = {"Normal", "Multiply", "Screen", "Add", "Overlay",
                                    "Soft Light", "Darken", "Lighten", "Color Burn", "Color Dodge"};

// Row widget for one layer. Own implementation; the desktop LayerRowWidget is
// not reused because its 42 px density and drag handling are mouse-oriented.
class MobileLayerRow : public QWidget {
public:
  MobileLayerRow(core::Document &doc, std::size_t index, const QPixmap &thumb, QWidget *parent)
      : QWidget(parent), doc_(doc), index_(index) {
    setMinimumHeight(64);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);

    const auto &layer = doc_.layer(index_);
    const int depth = doc_.get_layer_depth(index_);

    auto *thumb_label = new QLabel(this);
    thumb_label->setFixedSize(40, 40);
    thumb_label->setPixmap(thumb);
    layout->addSpacing(depth * 16);
    layout->addWidget(thumb_label);

    auto *text_col = new QVBoxLayout();
    text_col->setSpacing(0);
    name_label_ = new QLabel(this);
    name_label_->setFont(font_body());
    name_label_->setText(QString::fromStdString(layer.name()));
    auto *sub = new QLabel(this);
    sub->setProperty("muted", true);
    sub->setFont(font_caption());
    sub->setText(QString("%1  %2%")
                     .arg(QString::fromStdString(layer.composite_op()))
                     .arg(static_cast<int>(std::lround(layer.opacity() * 100.0))));
    text_col->addWidget(name_label_);
    text_col->addWidget(sub);
    layout->addLayout(text_col, 1);

    eye_btn_ = new QPushButton(this);
    eye_btn_->setFixedSize(44, 44);
    eye_btn_->setFlat(true);
    eye_btn_->setCursor(Qt::PointingHandCursor);
    eye_btn_->setIcon(IconHelper::load_symbolic(
        layer.visible() ? ":/icons/icons/layer-visible-symbolic.svg"
                        : ":/icons/icons/layer-hidden-symbolic.svg",
        tokens().text_muted, 20));
    eye_btn_->setStyleSheet("background: transparent; border: none;");
    layout->addWidget(eye_btn_);

    lock_btn_ = new QPushButton(this);
    lock_btn_->setFixedSize(44, 44);
    lock_btn_->setFlat(true);
    lock_btn_->setCursor(Qt::PointingHandCursor);
    lock_btn_->setIcon(IconHelper::load_symbolic(
        layer.locked() ? ":/icons/icons/layer-lock-symbolic.svg"
                       : ":/icons/icons/layer-unlock-symbolic.svg",
        layer.locked() ? tokens().accent : tokens().text_muted, 20));
    lock_btn_->setStyleSheet("background: transparent; border: none;");
    layout->addWidget(lock_btn_);

    auto *drag = new QLabel(this);
    drag->setFixedSize(44, 44);
    drag->setAlignment(Qt::AlignCenter);
    drag->setPixmap(IconHelper::render_symbolic(":/icons/icons/drag-handle-symbolic.svg",
                                               tokens().text_muted, 20));
    drag->setCursor(Qt::SizeVerCursor);
    layout->addWidget(drag);
  }

  QPushButton *eye_button() const { return eye_btn_; }
  QPushButton *lock_button() const { return lock_btn_; }

private:
  core::Document &doc_;
  std::size_t index_;
  QLabel *name_label_ = nullptr;
  QPushButton *eye_btn_ = nullptr;
  QPushButton *lock_btn_ = nullptr;
};

struct LayerSheetState {
  core::Document *doc = nullptr;
  Canvas *canvas = nullptr;
  std::function<void()> on_structure_changed;
  std::function<void(const QString &)> hud;
  std::function<void(const QString &)> open_sheet;
  QListWidget *list = nullptr;
  QSlider *opacity = nullptr;
  QComboBox *blend = nullptr;
  QComboBox *screentone = nullptr;
  // Keyed by layer id + active flag; render_layer_thumbnail is expensive.
  std::unordered_map<std::string, QPixmap> thumb_cache;
  bool rebuilding = false;
};

void refresh_layer_rows(LayerSheetState &st);

void commit(LayerSheetState &st, const std::function<void()> &action) {
  if (!st.doc) return;
  st.thumb_cache.clear();
  st.doc->begin_change();
  action();
  st.doc->commit_change();
  st.doc->mark_composite_dirty();
  if (st.canvas) st.canvas->refresh();
  refresh_layer_rows(st);
  if (st.on_structure_changed) st.on_structure_changed();
}

void refresh_layer_rows(LayerSheetState &st) {
  if (!st.list || !st.doc || st.rebuilding) return;
  st.rebuilding = true;
  st.list->clear();

  auto &doc = *st.doc;
  // Top-most first.
  for (std::size_t i = doc.layer_count(); i-- > 0;) {
    const auto &layer = doc.layer(i);
    const bool is_active = (i == doc.active_layer_index());
    // Thumbnails are cached per layer id + active state, so reopening the sheet
    // does not rasterise every layer again.
    const std::string key = std::to_string(layer.id()) + (is_active ? ":a" : ":i");
    auto cache_it = st.thumb_cache.find(key);
    QPixmap thumb;
    if (cache_it != st.thumb_cache.end()) {
      thumb = cache_it->second;
    } else {
      thumb = render_layer_thumbnail(layer, 40, is_active);
      st.thumb_cache.emplace(key, thumb);
    }
    auto *item = new QListWidgetItem(st.list);
    item->setSizeHint(QSize(180, 64));
    item->setData(Qt::UserRole, static_cast<qulonglong>(i));
    if (i == doc.active_layer_index()) {
      item->setSelected(true);
    }

    auto *row = new MobileLayerRow(doc, i, thumb, st.list);
    QObject::connect(row->eye_button(), &QPushButton::clicked, row, [&st, i]() {
      if (!st.doc) return;
      st.doc->begin_change();
      st.doc->set_layer_visible(i, !st.doc->layer(i).visible());
      st.doc->commit_change();
      st.doc->mark_composite_dirty();
      if (st.canvas) st.canvas->refresh();
      refresh_layer_rows(st);
    });
    QObject::connect(row->lock_button(), &QPushButton::clicked, row, [&st, i]() {
      if (!st.doc) return;
      st.doc->begin_change();
      st.doc->layer(i).set_locked(!st.doc->layer(i).locked());
      st.doc->commit_change();
      refresh_layer_rows(st);
    });
    st.list->setItemWidget(item, row);
  }

  // Sync the bottom controls with the active layer.
  if (doc.has_active_layer() && doc.active_layer_index() < doc.layer_count()) {
    const auto &active = doc.layer(doc.active_layer_index());
    st.opacity->blockSignals(true);
    st.opacity->setValue(static_cast<int>(std::lround(active.opacity() * 100.0)));
    st.opacity->blockSignals(false);
    const QString op = QString::fromStdString(active.composite_op());
    for (int i = 0; i < static_cast<int>(std::size(kBlendModes)); ++i) {
      if (op == QLatin1String(kBlendModes[i])) {
        st.blend->setCurrentIndex(i);
        break;
      }
    }
    st.screentone->setCurrentIndex(active.screentone() ? 1 : 0);
  }
  st.rebuilding = false;
}

} // namespace

void build_layer_sheet(MobileSheet *sheet, EditorContext &ctx, EditorSheets &sheets) {
  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto *state = new LayerSheetState();
  state->doc = ctx.doc;
  state->canvas = ctx.canvas;
  state->on_structure_changed = ctx.document_structure_changed;
  state->hud = ctx.hud;
  state->open_sheet = ctx.open_sheet;
  QObject::connect(sheet, &QObject::destroyed, body, [state]() { delete state; });

  // Header row
  auto *header = new QHBoxLayout();
  header->setSpacing(8);
  auto *selection_btn = new QPushButton(body);
  selection_btn->setFixedSize(44, 44);
  selection_btn->setCheckable(true);
  selection_btn->setCursor(Qt::PointingHandCursor);
  selection_btn->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-rect-select-symbolic.svg",
                                                  tokens().text, 20));
  selection_btn->setToolTip(QObject::tr("Selection layer mode"));
  header->addWidget(selection_btn);

  auto *add_btn = new QPushButton(body);
  add_btn->setFixedSize(44, 44);
  add_btn->setIcon(IconHelper::load_symbolic(":/icons/icons/list-add-symbolic.svg", tokens().text, 20));
  header->addWidget(add_btn);

  auto *group_btn = new QPushButton(QObject::tr("Group"), body);
  group_btn->setMinimumHeight(44);
  header->addWidget(group_btn);
  header->addStretch(1);
  layout->addLayout(header);

  auto *list = new QListWidget(body);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setDragDropMode(QAbstractItemView::InternalMove);
  layout->addWidget(list, 1);
  state->list = list;

  // Bottom controls
  auto *opacity_row = new QHBoxLayout();
  auto *opacity_label = new QLabel(QObject::tr("Opacity"), body);
  opacity_label->setProperty("muted", true);
  state->opacity = new QSlider(Qt::Horizontal, body);
  state->opacity->setRange(0, 100);
  state->opacity->setMinimumHeight(44);
  auto *opacity_value = new QLabel(body);
  opacity_row->addWidget(opacity_label);
  opacity_row->addWidget(state->opacity, 1);
  opacity_row->addWidget(opacity_value);
  layout->addLayout(opacity_row);

  auto *blend_row = new QHBoxLayout();
  auto *blend_label = new QLabel(QObject::tr("Blend"), body);
  blend_label->setProperty("muted", true);
  state->blend = new QComboBox(body);
  for (const char *label : kBlendLabels) state->blend->addItem(QString::fromUtf8(label));
  blend_row->addWidget(blend_label);
  blend_row->addWidget(state->blend, 1);
  layout->addLayout(blend_row);

  auto *tone_row = new QHBoxLayout();
  auto *tone_label = new QLabel(QObject::tr("Screentone"), body);
  tone_label->setProperty("muted", true);
  state->screentone = new QComboBox(body);
  state->screentone->addItems({QObject::tr("Off"), QObject::tr("Dot"), QObject::tr("Line"),
                               QObject::tr("Hatch")});
  tone_row->addWidget(tone_label);
  tone_row->addWidget(state->screentone, 1);
  layout->addLayout(tone_row);

  sheet->set_body(body);

  // --- Wiring -------------------------------------------------------------

  QObject::connect(selection_btn, &QPushButton::toggled, body, [ctx](bool on) {
    if (ctx.canvas) ctx.canvas->set_selection_layer_mode(on);
  });

  QObject::connect(add_btn, &QPushButton::clicked, body, [state]() {
    commit(*state, [state]() {
      state->doc->add_layer();
      state->doc->set_active_layer_index(state->doc->layer_count() - 1);
    });
  });

  QObject::connect(group_btn, &QPushButton::clicked, body, [state]() {
    commit(*state, [state]() {
      state->doc->add_group();
      state->doc->set_active_layer_index(state->doc->layer_count() - 1);
    });
  });

  QObject::connect(list, &QListWidget::itemClicked, body, [state](QListWidgetItem *item) {
    if (!state->doc || !item) return;
    const auto index = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
    if (index >= state->doc->layer_count()) return;
    // Selecting a layer must not open a history transaction: snapshots_differ
    // compares active_layer_index, so it would record a bogus undo step.
    state->doc->set_active_layer_index(index);
    refresh_layer_rows(*state);
  });

  QObject::connect(state->opacity, &QSlider::valueChanged, body, [state, opacity_value](int v) {
    opacity_value->setText(QString::number(v) + "%");
    if (!state->doc || state->rebuilding || !state->doc->has_active_layer()) return;
    state->doc->set_layer_opacity(state->doc->active_layer_index(), v / 100.0);
    state->doc->mark_composite_dirty();
    if (state->canvas) state->canvas->update();
  });

  QObject::connect(state->blend, &QComboBox::currentIndexChanged, body, [state](int idx) {
    if (!state->doc || state->rebuilding || !state->doc->has_active_layer()) return;
    if (idx < 0 || idx >= static_cast<int>(std::size(kBlendModes))) return;
    state->doc->begin_change();
    state->doc->set_layer_composite_op(state->doc->active_layer_index(), kBlendModes[idx]);
    state->doc->commit_change();
    state->doc->mark_composite_dirty();
    if (state->canvas) state->canvas->refresh();
  });

  QObject::connect(state->screentone, &QComboBox::currentIndexChanged, body, [state](int idx) {
    if (!state->doc || state->rebuilding || !state->doc->has_active_layer()) return;
    const std::size_t active = state->doc->active_layer_index();
    state->doc->begin_change();
    state->doc->set_layer_screentone(active, idx != 0);
    if (idx == 1) state->doc->set_layer_screentone_pattern(active, core::ScreentonePattern::DOT_60);
    else if (idx == 2) state->doc->set_layer_screentone_pattern(active, core::ScreentonePattern::LINE_45);
    else if (idx == 3) state->doc->set_layer_screentone_pattern(active, core::ScreentonePattern::CROSSHATCH);
    state->doc->commit_change();
    state->doc->mark_composite_dirty();
    if (state->canvas) state->canvas->refresh();
  });

  // Long press on a row opens the action sheet.
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(list, &QListWidget::customContextMenuRequested, body,
                   [state, sheet, &sheets](const QPoint &pos) {
                     QListWidgetItem *item = state->list->itemAt(pos);
                     if (!item || !state->doc) return;
                     const auto index = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
                     if (index >= state->doc->layer_count()) return;

                     auto &doc = *state->doc;
                     doc.set_active_layer_index(index);
                     const bool is_group = doc.layer(index).is_group();
                     const bool can_merge = index > 0;

                     auto *action_sheet = new MobileSheet(sheets.host(), QObject::tr("Layer"), true);
                     auto *action_body = new QWidget(action_sheet);
                     auto *action_layout = new QVBoxLayout(action_body);
                     action_layout->setContentsMargins(0, 0, 0, 0);
                     action_layout->setSpacing(8);

                     auto add_action = [&](const QString &label, bool enabled,
                                           std::function<void()> fn) {
                       auto *btn = new QPushButton(label, action_body);
                       btn->setMinimumHeight(48);
                       btn->setEnabled(enabled);
                       btn->setCursor(Qt::PointingHandCursor);
                       QObject::connect(btn, &QPushButton::clicked, action_body,
                                        [action_sheet, fn]() {
                                          action_sheet->close();
                                          if (fn) fn();
                                        });
                       action_layout->addWidget(btn);
                     };

                     add_action(QObject::tr("Rename"), true, [state, index, &sheets]() {
                       const std::string current = state->doc->layer(index).name();
                       auto *text_sheet = sheets.make_text_sheet(
                           QObject::tr("Rename Layer"), QObject::tr("Name"),
                           QString::fromStdString(current),
                           [state, index](const QString &text, QString &error) {
                             if (text.isEmpty()) {
                               error = QObject::tr("Name cannot be empty");
                               return false;
                             }
                             commit(*state, [state, index, text]() {
                               state->doc->rename_layer(index, text.toStdString());
                             });
                             return true;
                           });
                       sheets.show_sheet_public(text_sheet);
                     });

                     add_action(QObject::tr("Duplicate"), !is_group, [state, index]() {
                       commit(*state, [state, index]() {
                         auto &doc = *state->doc;
                         if (!doc.duplicate_layer(index)) return;
                         // Duplicate copies no ID; assign a unique one so GL
                         // layer-texture caching and undo restore stay correct.
                         std::size_t max_id = 0;
                         for (std::size_t i = 0; i < doc.layer_count(); ++i) {
                           max_id = std::max(max_id, doc.layer(i).id());
                         }
                         const std::size_t new_index = std::min(index + 1, doc.layer_count() - 1);
                         doc.layer(new_index).set_id(max_id + 1);
                         doc.set_active_layer_index(new_index);
                       });
                     });

                     add_action(QObject::tr("Merge Down"), can_merge, [state, index]() {
                       commit(*state, [state, index]() {
                         auto &doc = *state->doc;
                         if (doc.layer(index).is_group()) {
                           doc.merge_group(index);
                         } else {
                           doc.merge_layer_down(index);
                         }
                         doc.set_active_layer_index(index > 0 ? index - 1 : 0);
                       });
                     });

                     add_action(doc.layer(index).clipping_mask() ? QObject::tr("Release Clipping Mask")
                                                                 : QObject::tr("Clipping Mask"),
                                true, [state, index]() {
                                  commit(*state, [state, index]() {
                                    auto &doc = *state->doc;
                                    doc.set_layer_clipping_mask(index, !doc.layer(index).clipping_mask());
                                  });
                                });

                     add_action(doc.layer(index).alpha_locked() ? QObject::tr("Unlock Alpha")
                                                                : QObject::tr("Alpha Lock"),
                                true, [state, index]() {
                                  commit(*state, [state, index]() {
                                    auto &doc = *state->doc;
                                    doc.set_layer_alpha_locked(index, !doc.layer(index).alpha_locked());
                                  });
                                });

                     add_action(QObject::tr("Move Up"), index + 1 < doc.layer_count(),
                                [state, index]() {
                                  commit(*state, [state, index]() {
                                    auto &doc = *state->doc;
                                    doc.move_layer(index, index + 1);
                                    doc.clean_hierarchy_invariants();
                                    doc.set_active_layer_index(index + 1);
                                  });
                                });

                     add_action(QObject::tr("Move Down"), index > 0, [state, index]() {
                       commit(*state, [state, index]() {
                         auto &doc = *state->doc;
                         doc.move_layer(index, index - 1);
                         doc.clean_hierarchy_invariants();
                         doc.set_active_layer_index(index - 1);
                       });
                     });

                     add_action(QObject::tr("Flip Horizontal"), !is_group, [state, index]() {
                       commit(*state, [state, index]() {
                         auto &doc = *state->doc;
                         auto &layer = doc.layer(index);
                         const int w = layer.image().width();
                         const int h = layer.image().height();
                         core::TiledImage flipped(w, h);
                         for (const auto &[key, tile] : layer.image().tiles()) {
                           const int tx = core::TiledImage::key_x(key);
                           const int ty = core::TiledImage::key_y(key);
                           for (int ly = 0; ly < core::kTileSize; ++ly) {
                             const int doc_y = ty * core::kTileSize + ly;
                             if (doc_y >= h) break;
                             for (int lx = 0; lx < core::kTileSize; ++lx) {
                               const int doc_x = tx * core::kTileSize + lx;
                               if (doc_x >= w) break;
                               const auto c = tile->pixels[ly * core::kTileSize + lx];
                               if (c.a > 0) flipped.set_pixel(w - 1 - doc_x, doc_y, c);
                             }
                           }
                         }
                         layer.image() = std::move(flipped);
                       });
                     });

                     add_action(QObject::tr("Flip Vertical"), !is_group, [state, index]() {
                       commit(*state, [state, index]() {
                         auto &doc = *state->doc;
                         auto &layer = doc.layer(index);
                         const int w = layer.image().width();
                         const int h = layer.image().height();
                         core::TiledImage flipped(w, h);
                         for (const auto &[key, tile] : layer.image().tiles()) {
                           const int tx = core::TiledImage::key_x(key);
                           const int ty = core::TiledImage::key_y(key);
                           for (int ly = 0; ly < core::kTileSize; ++ly) {
                             const int doc_y = ty * core::kTileSize + ly;
                             if (doc_y >= h) break;
                             for (int lx = 0; lx < core::kTileSize; ++lx) {
                               const int doc_x = tx * core::kTileSize + lx;
                               if (doc_x >= w) break;
                               const auto c = tile->pixels[ly * core::kTileSize + lx];
                               if (c.a > 0) flipped.set_pixel(doc_x, h - 1 - doc_y, c);
                             }
                           }
                         }
                         layer.image() = std::move(flipped);
                       });
                     });

                     add_action(QObject::tr("Delete"), true, [state, index, &sheets]() {
                       auto &doc = *state->doc;
                       if (doc.layer_count() <= 1) {
                         if (state->hud) state->hud(QObject::tr("Cannot delete the last layer"));
                         return;
                       }
                       if (doc.layer(index).is_group()) {
                         std::size_t layers = 0, groups = 0;
                         doc.get_group_descendants_count(index, layers, groups);
                         if (layers + groups > 0) {
                           auto *confirm = sheets.make_confirm_sheet(
                               QObject::tr("Delete Group"),
                               QObject::tr("This group contains %1 layers. Delete everything?").arg(layers),
                               QObject::tr("Delete"), true, [state, index]() {
                                 commit(*state, [state, index]() {
                                   auto &d = *state->doc;
                                   d.remove_group_recursive(index);
                                   d.set_active_layer_index(std::min(index, d.layer_count() - 1));
                                 });
                               });
                           sheets.show_sheet_public(confirm);
                           return;
                         }
                       }
                       commit(*state, [state, index]() {
                         auto &d = *state->doc;
                         d.remove_layer(index);
                         d.set_active_layer_index(std::min(index, d.layer_count() - 1));
                       });
                     });

                     action_sheet->set_body(action_body);
                     sheets.register_transient(action_sheet);
                     sheets.show_sheet_public(action_sheet);
                   });

  // Reorder via the drag handle: QListWidget InternalMove then remap.
  QObject::connect(list->model(), &QAbstractItemModel::rowsMoved, body,
                   [state, list](const QModelIndex &, int start, int, const QModelIndex &, int dest) {
                     if (!state->doc || state->rebuilding) return;
                     auto &doc = *state->doc;
                     const int count = static_cast<int>(doc.layer_count());
                     if (count <= 1) return;
                     const int from_row = start;
                     int to_row = dest > start ? dest - 1 : dest;
                     if (from_row == to_row) return;
                     const auto from_index = static_cast<std::size_t>(count - 1 - from_row);
                     const auto target_index = static_cast<std::size_t>(count - 1 - to_row);
                     if (from_index >= doc.layer_count() || target_index >= doc.layer_count()) return;

                     doc.begin_change();
                     doc.move_layer_relative(from_index, target_index, to_row > from_row);
                     doc.clean_hierarchy_invariants();
                     doc.commit_change();
                     doc.mark_composite_dirty();
                     if (state->canvas) state->canvas->refresh();
                     refresh_layer_rows(*state);
                     if (state->on_structure_changed) state->on_structure_changed();
                   });

  refresh_layer_rows(*state);
  sheet->set_on_open([state]() {
    state->thumb_cache.clear();
    refresh_layer_rows(*state);
  });
}

void notify_layer_sheet_opened(MobileSheet *sheet) {
  // The sheet rebuilds itself from its own state when it becomes visible; the
  // heavy work is deferred until the user actually looks at the list.
  if (!sheet) return;
  if (auto *list = sheet->findChild<QListWidget *>()) {
    list->update();
  }
}

} // namespace kreart::ui::mobile
