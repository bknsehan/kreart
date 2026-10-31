#include "kreart/ui/layer_panel.h"
#include "kreart/ui/canvas.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/app_settings.h"

#include <QFileDialog>
#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainterPath>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QStyledItemDelegate>

class LayerItemDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override {
    // Custom row widget handles all row visuals; suppress duplicate native cell text/icons
  }
};

namespace kreart::ui {
static void draw_transparency_checkerboard(QPainter &painter, int w, int h, int check_size) {
  const auto &s = AppSettings::instance();
  const bool is_dark = (s.theme() == ThemeMode::DARK ||
                       (s.theme() == ThemeMode::SYSTEM &&
                        QApplication::palette().color(QPalette::Window).value() < 128));
  const auto trans_mode = s.effective_transparency_mode(is_dark);

  if (trans_mode == TransparencyMode::SOLID_COLOR) {
    painter.fillRect(0, 0, w, h, s.transparency_solid_color());
  } else {
    const QColor c1 = (trans_mode == TransparencyMode::DARK_CHECKERBOARD)
                          ? QColor(56, 59, 66)
                          : QColor("#ffffff");
    const QColor c2 = (trans_mode == TransparencyMode::DARK_CHECKERBOARD)
                          ? QColor(41, 43, 48)
                          : QColor("#e0e0e0");
    for (int y = 0; y < h; y += check_size) {
      for (int x = 0; x < w; x += check_size) {
        const bool is_c1 = ((x / check_size) + (y / check_size)) % 2 == 0;
        painter.fillRect(x, y, std::min(check_size, w - x), std::min(check_size, h - y),
                         is_c1 ? c1 : c2);
      }
    }
  }
}

QPixmap render_layer_thumbnail(const core::Layer &layer, int size, bool is_active) {
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);

  draw_transparency_checkerboard(painter, size, size, 6);

  // 2. Downsampled layer image
  if (!layer.is_group() && !layer.image().tiles().empty()) {
    const int doc_w = layer.image().width();
    const int doc_h = layer.image().height();
    if (doc_w > 0 && doc_h > 0) {
      QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      for (int py = 0; py < size; ++py) {
        const int sy = (py * doc_h) / size;
        for (int px = 0; px < size; ++px) {
          const int sx = (px * doc_w) / size;
          const auto c = layer.image().pixel(sx, sy);
          if (c.a > 0) {
            img.setPixelColor(px, py, QColor(c.r, c.g, c.b, c.a));
          }
        }
      }
      painter.drawImage(0, 0, img);
    }
  }

  // 3. Folder overlay in bottom-left corner if folder
  if (layer.is_group()) {
    QPixmap folder_icon = IconHelper::render_symbolic(":/icons/icons/folder-symbolic.svg", QColor(80, 80, 80), 20);
    painter.drawPixmap(2, size - 22, folder_icon);
  }

  // 4. Downward right-angle arrow indicator if clipping mask
  if (layer.clipping_mask()) {
    painter.setPen(QColor(40, 40, 40));
    QFont font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(2, size - 3, QString::fromUtf8("↳"));
  }

  // 5. Active layer: 2px blue outline (#3b82f6) with 4px border radius
  if (is_active) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#3b82f6"), 2.0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.0, 1.0, size - 2.0, size - 2.0), 2.0, 2.0);
  }

  return pixmap;
}
QPixmap render_layer_preview(const core::Layer &layer, int max_w, int max_h) {
  const int doc_w = std::max(1, layer.image().width());
  const int doc_h = std::max(1, layer.image().height());

  const double scale = std::min(static_cast<double>(max_w) / doc_w, static_cast<double>(max_h) / doc_h);
  const int pw = std::max(16, static_cast<int>(std::round(doc_w * scale)));
  const int ph = std::max(16, static_cast<int>(std::round(doc_h * scale)));

  QPixmap pixmap(pw, ph);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);

  draw_transparency_checkerboard(painter, pw, ph, 8);

  if (!layer.is_group() && !layer.image().tiles().empty()) {
    QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int py = 0; py < ph; ++py) {
      const int sy = (py * doc_h) / ph;
      for (int px = 0; px < pw; ++px) {
        const int sx = (px * doc_w) / pw;
        const auto c = layer.image().pixel(sx, sy);
        if (c.a > 0) {
          img.setPixelColor(px, py, QColor(c.r, c.g, c.b, c.a));
        }
      }
    }
    painter.drawImage(0, 0, img);
  }

  painter.setPen(QColor(180, 180, 180));
  painter.drawRect(0, 0, pw - 1, ph - 1);
  return pixmap;
}

QPixmap render_selection_preview(const core::Document &doc, const Canvas *canvas, int max_w, int max_h) {
  const int doc_w = std::max(1, doc.width());
  const int doc_h = std::max(1, doc.height());

  const double scale = std::min(static_cast<double>(max_w) / doc_w, static_cast<double>(max_h) / doc_h);
  const int pw = std::max(16, static_cast<int>(std::round(doc_w * scale)));
  const int ph = std::max(16, static_cast<int>(std::round(doc_h * scale)));

  QPixmap pixmap(pw, ph);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);

  draw_transparency_checkerboard(painter, pw, ph, 8);

  // 2. Draw downsampled document composite from doc.composite_image()
  const auto &comp = doc.composite_image();
  if (!comp.tiles().empty()) {
    QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int py = 0; py < ph; ++py) {
      const int sy = (py * doc_h) / ph;
      for (int px = 0; px < pw; ++px) {
        const int sx = (px * doc_w) / pw;
        const auto c = comp.pixel(sx, sy);
        if (c.a > 0) {
          img.setPixelColor(px, py, QColor(c.r, c.g, c.b, c.a));
        }
      }
    }
    painter.drawImage(0, 0, img);
  }

  // 3. Selection overlay if selection is active
  const bool sel_mode = canvas && canvas->selection_layer_mode();
  const bool sel_active = doc.selection().active() || sel_mode;
  if (sel_active) {
    QImage overlay(pw, ph, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    const QColor sel_color(244, 63, 94, 110);
    // A preview pixel is selected if ANY document pixel within the span it covers
    // is selected. Scanning a single centre point made thin / one-pixel-wide
    // selections vanish or render as disjoint fragments when downsampled. The
    // span scan visits every document pixel exactly once, so the preview stays
    // fully connected for selections of any thickness.
    for (int py = 0; py < ph; ++py) {
      const int sy0 = (py * doc_h) / ph;
      const int sy1 = ((py + 1) * doc_h) / ph; // exclusive
      for (int px = 0; px < pw; ++px) {
        const int sx0 = (px * doc_w) / pw;
        const int sx1 = ((px + 1) * doc_w) / pw; // exclusive
        bool found = false;
        for (int sy = sy0; sy < std::max(sy1, sy0 + 1) && !found; ++sy) {
          for (int sx = sx0; sx < std::max(sx1, sx0 + 1); ++sx) {
            bool is_sel = false;
            if (sel_mode) {
              is_sel = (canvas->selection_image().pixel(sx, sy).a > 0);
            } else {
              is_sel = doc.selection().is_selected(sx, sy);
            }
            if (is_sel) {
              found = true;
              break;
            }
          }
        }
        if (found) {
          overlay.setPixelColor(px, py, sel_color);
        }
      }
    }
    painter.drawImage(0, 0, overlay);
  }

  // 4. Draw 1px border (#b4b4b4)
  painter.setPen(QColor("#b4b4b4"));
  painter.drawRect(0, 0, pw - 1, ph - 1);
  return pixmap;
}

// ============================================================================
// SelectionLayerCard Implementation
// ============================================================================

SelectionLayerCard::SelectionLayerCard(QWidget *parent) : QFrame(parent) {
  setObjectName("SelectionLayerCard");
  setCursor(Qt::PointingHandCursor);
  setFixedHeight(42);
  setStyleSheet(
      "#SelectionLayerCard { background: rgba(244, 63, 94, 0.08); border: 1px dashed rgba(244, 63, 94, 0.4); "
      "border-radius: 6px; padding: 2px; } "
      "#SelectionLayerCard:hover { background: rgba(244, 63, 94, 0.16); }");

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 2, 6, 2);
  layout->setSpacing(6);

  // Spacer matching chevron width (18x18) for non-group items
  spacer_ = new QWidget(this);
  spacer_->setFixedSize(18, 18);
  spacer_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(spacer_);

  // Selection Icon badge (16x16 with tool-rect-select-symbolic.svg rendered in #f43f5e)
  icon_label_ = new QLabel(this);
  icon_label_->setFixedSize(16, 16);
  icon_label_->setPixmap(IconHelper::render_symbolic(":/icons/icons/tool-rect-select-symbolic.svg", QColor("#f43f5e"), 16));
  icon_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(icon_label_);

  // Name and Subtitle Column
  auto *text_col = new QVBoxLayout();
  text_col->setSpacing(0);
  text_col->setContentsMargins(0, 1, 0, 1);

  title_label_ = new QLabel(tr("Selection Layer"), this);
  QFont tf = title_label_->font();
  tf.setBold(true);
  title_label_->setFont(tf);
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  text_col->addWidget(title_label_);

  subtitle_label_ = new QLabel(tr("No Selection"), this);
  subtitle_label_->setStyleSheet("color: gray; font-size: 11px;");
  subtitle_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  text_col->addWidget(subtitle_label_);

  layout->addLayout(text_col, 1);

  // Camera preview button matching LayerRowWidget::preview_btn_ (22x22)
  preview_btn_ = new QPushButton(this);
  preview_btn_->setFixedSize(22, 22);
  preview_btn_->setFlat(true);
  preview_btn_->setCursor(Qt::PointingHandCursor);
  preview_btn_->setToolTip(tr("Preview Selection Layer"));
  preview_btn_->setStyleSheet(
      "QPushButton { border: none; background: transparent; padding: 0px; border-radius: 4px; }"
      "QPushButton:hover { background: rgba(244, 63, 94, 0.25); }"
      "QPushButton:pressed { background: rgba(244, 63, 94, 0.40); }");
  preview_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/camera-symbolic.svg", palette(), 14));
  connect(preview_btn_, &QPushButton::clicked, this, &SelectionLayerCard::previewClicked);
  layout->addWidget(preview_btn_);

  // Trailing spacer matching eye_btn_ (22) + drag_handle_ (16) + spacing (6) = 44px
  // so preview_btn_ aligns in the exact vertical column as on LayerRowWidget
  auto *trailing_spacer = new QWidget(this);
  trailing_spacer->setFixedSize(28, 22);
  trailing_spacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  layout->addWidget(trailing_spacer);
}

void SelectionLayerCard::update_status(bool active, int w, int h) {
  if (subtitle_label_) {
    if (active && w > 0 && h > 0) {
      subtitle_label_->setText(tr("Selection Active (%1x%2)").arg(w).arg(h));
    } else {
      subtitle_label_->setText(tr("No Selection"));
    }
  }
}

void SelectionLayerCard::set_selected(bool sel) {
  selected_ = sel;
  if (selected_) {
    setStyleSheet(
        "#SelectionLayerCard { background: rgba(244, 63, 94, 0.22); border: 2px solid #3b82f6; "
        "border-radius: 6px; padding: 2px; }");
  } else {
    setStyleSheet(
        "#SelectionLayerCard { background: rgba(244, 63, 94, 0.08); border: 1px dashed rgba(244, 63, 94, 0.4); "
        "border-radius: 6px; padding: 2px; } "
        "#SelectionLayerCard:hover { background: rgba(244, 63, 94, 0.16); }");
  }
}

void SelectionLayerCard::mousePressEvent(QMouseEvent *event) {
  emit clicked();
  QFrame::mousePressEvent(event);
}

// ============================================================================
// LayerTreeWidget Implementation
// ============================================================================

LayerTreeWidget::LayerTreeWidget(core::Document &doc, Canvas *canvas, QWidget *parent)
    : QTreeWidget(parent), document_(doc), canvas_(canvas) {
  setColumnCount(4);
  setHeaderHidden(true);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setRootIsDecorated(false);
  setIndentation(0);
  setItemDelegate(new LayerItemDelegate(this));
  // The transient horizontal scrollbar appearing/disappearing while expanding a
  // group with nested groups made the whole tree glow/re-layout ("weird animation").
  // Rows never need horizontal scrolling; disabling it also keeps heights uniform.
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setUniformRowHeights(true);

  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::DragDrop);

  // Hide auxiliary columns so column 1 stretches across full row while preserving item data
  header()->hideSection(0);
  header()->setSectionResizeMode(1, QHeaderView::Stretch);
  header()->hideSection(2);
  header()->hideSection(3);
}

bool LayerTreeWidget::perform_drop(std::size_t from_idx, std::size_t target_idx, double rel_y) {
  const std::size_t n = document_.layer_count();
  if (from_idx >= n || target_idx >= n || from_idx == target_idx) {
    return false;
  }

  const std::size_t from_id = document_.layer(from_idx).id();
  const std::size_t target_id = document_.layer(target_idx).id();
  const bool target_is_group = document_.layer(target_idx).is_group();

  // Reject dropping a group onto itself or any of its own descendants (circular hierarchy)
  if (target_id == from_id || document_.is_descendant_of(target_id, from_id)) {
    return false;
  }

  // The middle band of a folder row means "drop into folder".
  const bool into_folder = target_is_group && rel_y >= 0.28 && rel_y <= 0.72;
  // Below an expanded folder header also means "become topmost child of that folder".
  const bool under_expanded_folder =
      target_is_group && rel_y > 0.72 && document_.layer(target_idx).expanded();

  document_.begin_change();
  if (into_folder || under_expanded_folder) {
    document_.move_into_group(from_idx, target_idx);
  } else {
    std::size_t target_pid = document_.layer(target_idx).parent_id();
    if (target_pid != 0 && rel_y >= 0.5) {
      // Dropping below the bottom-most child of a folder exits the folder:
      // the moved layer becomes a sibling of that folder, placed right below it.
      bool is_lowest_child = true;
      for (std::size_t c = 0; c < target_idx; ++c) {
        if (document_.layer(c).parent_id() == target_pid) {
          is_lowest_child = false;
          break;
        }
      }
      if (is_lowest_child) {
        if (const core::Layer *folder = document_.find_layer_by_id(target_pid)) {
          target_pid = folder->parent_id();
        } else {
          target_pid = 0;
        }
      }
    }
    document_.layer(from_idx).set_parent_id(target_pid);
    document_.move_layer_relative(from_idx, target_idx, rel_y >= 0.5);
  }

  document_.clean_hierarchy_invariants();
  document_.commit_change();
  if (canvas_) canvas_->refresh();
  if (rebuild_cb_) {
    // Defer the rebuild so it runs outside the active drag/drop event
    QTimer::singleShot(0, [this]() {
      if (rebuild_cb_) rebuild_cb_();
    });
  }
  return true;
}

void LayerTreeWidget::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat("application/x-kreart-layer") || event->source() == this) {
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}
void LayerTreeWidget::dragMoveEvent(QDragMoveEvent *event) {
  QTreeWidgetItem *target_item = itemAt(event->position().toPoint());
  if (!target_item) {
    event->acceptProposedAction();
    return;
  }

  const bool is_folder = target_item->data(0, Qt::UserRole + 1).toBool();
  const QRect r = visualItemRect(target_item);
  const double rel_y = (event->position().y() - r.top()) / static_cast<double>(std::max(1, r.height()));

  if (!is_folder && rel_y >= 0.25 && rel_y <= 0.75) {
    // Normal layers reject OnItem drops; accepted for above/below reordering
    event->acceptProposedAction();
  } else if (is_folder && rel_y >= 0.28 && rel_y <= 0.72) {
    // Accept drop into folder
    event->acceptProposedAction();
  } else {
    event->acceptProposedAction();
  }
}

void LayerTreeWidget::dropEvent(QDropEvent *event) {
  int from_idx = -1;
  if (event->mimeData() && event->mimeData()->hasFormat("application/x-kreart-layer")) {
    QByteArray data = event->mimeData()->data("application/x-kreart-layer");
    QDataStream stream(&data, QIODevice::ReadOnly);
    qint32 idx;
    stream >> idx;
    from_idx = idx;
  } else if (currentItem()) {
    from_idx = currentItem()->data(0, Qt::UserRole).toInt();
  }

  if (from_idx < 0 || from_idx >= static_cast<int>(document_.layer_count())) {
    event->ignore();
    return;
  }

  QTreeWidgetItem *target_item = itemAt(event->position().toPoint());
  if (target_item) {
    const int target_idx = target_item->data(0, Qt::UserRole).toInt();
    const QRect r = visualItemRect(target_item);
    const double rel_y = (event->position().y() - r.top()) / static_cast<double>(std::max(1, r.height()));
    perform_drop(static_cast<std::size_t>(from_idx), static_cast<std::size_t>(target_idx), rel_y);
  } else {
    // Empty space drop (target_item == nullptr):
    // Move layer (with its children, if any) to the bottom of the document.
    document_.begin_change();
    document_.layer(from_idx).set_parent_id(0);
    document_.move_layer(static_cast<std::size_t>(from_idx), 0);
    document_.clean_hierarchy_invariants();
    document_.commit_change();
    if (canvas_) canvas_->refresh();
    if (rebuild_cb_) {
      QTimer::singleShot(0, [this]() {
        if (rebuild_cb_) rebuild_cb_();
      });
    }
  }

  event->acceptProposedAction();
}

// ============================================================================
// LayerRowWidget Implementation
// ============================================================================

LayerRowWidget::LayerRowWidget(std::size_t layer_index, const core::Layer &layer, bool is_active,
                               int child_count, int depth, LayerTreeWidget *tree, QWidget *parent)
    : QWidget(parent), layer_index_(layer_index), tree_widget_(tree) {
  setAttribute(Qt::WA_StyledBackground, true);
  setAttribute(Qt::WA_Hover, true);
  setAcceptDrops(true);
  setFixedHeight(42);
  setCursor(Qt::PointingHandCursor);
  // Type selectors ("LayerRowWidget {...}") do NOT match namespaced classes; use the
  // object-name selector (#LayerRowWidget) so background/hover/selected styles actually apply.
  setObjectName("LayerRowWidget");

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(4 + depth * 16, 2, 6, 2);
  layout->setSpacing(6);

  const auto &pal = palette();

  // Flat icon-button styling shared by every button in the row: transparent normally,
  // subtle pill highlight on hover so the cursor visibly "lands" on a clickable button.
  const QString btn_css =
      "QPushButton { border: none; background: transparent; padding: 0px; border-radius: 4px; }"
      "QPushButton:hover { background: rgba(255, 255, 255, 0.18); }"
      "QPushButton:pressed { background: rgba(255, 255, 255, 0.30); }";

  // 1. Expand/Collapse chevron for folder (or spacer for normal layer)
  if (layer.is_group()) {
    chevron_btn_ = new QPushButton(this);
    chevron_btn_->setFixedSize(18, 18);
    chevron_btn_->setFlat(true);
    chevron_btn_->setCursor(Qt::PointingHandCursor);
    chevron_btn_->setToolTip(tr(layer.expanded() ? "Collapse Group" : "Expand Group"));
    chevron_btn_->setStyleSheet(btn_css);
    const QString icon_path = layer.expanded() ? ":/icons/icons/pan-down-symbolic.svg"
                                              : ":/icons/icons/pan-end-symbolic.svg";
    chevron_btn_->setIcon(IconHelper::load_symbolic(icon_path, pal, 14));
    connect(chevron_btn_, &QPushButton::clicked, this, [this]() {
      emit expandToggled(layer_index_);
    });
    layout->addWidget(chevron_btn_);
  } else {
    auto *spacer = new QWidget(this);
    spacer->setFixedSize(18, 18);
    spacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(spacer);
  }

  // 2. Folder / Layer Icon Badge
  icon_label_ = new QLabel(this);
  icon_label_->setFixedSize(16, 16);
  icon_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  const QString type_icon = layer.is_group() ? ":/icons/icons/folder-symbolic.svg"
                                             : ":/icons/icons/layer-symbolic.svg";
  icon_label_->setPixmap(IconHelper::render_symbolic(type_icon, QColor(140, 140, 140), 16));
  layout->addWidget(icon_label_);

  // 3. Name and Subtitle Column
  auto *text_col = new QVBoxLayout();
  text_col->setSpacing(0);
  text_col->setContentsMargins(0, 1, 0, 1);

  name_label_ = new QLabel(QString::fromStdString(layer.name()), this);
  name_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  if (layer.is_group()) {
    QFont f = name_label_->font();
    f.setBold(true);
    name_label_->setFont(f);
  }
  text_col->addWidget(name_label_);

  QString subtitle = build_subtitle(layer, child_count);
  subtitle_label_ = new QLabel(subtitle, this);
  subtitle_label_->setStyleSheet("color: gray; font-size: 11px;");
  subtitle_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  text_col->addWidget(subtitle_label_);

  layout->addLayout(text_col, 1);

  // 4. Preview Button (shows preview dialog with canvas aspect ratio)
  preview_btn_ = new QPushButton(this);
  preview_btn_->setFixedSize(22, 22);
  preview_btn_->setFlat(true);
  preview_btn_->setCursor(Qt::PointingHandCursor);
  preview_btn_->setToolTip(tr("Preview Layer Contents"));
  preview_btn_->setStyleSheet(btn_css);
  preview_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/camera-symbolic.svg", pal, 14));
  connect(preview_btn_, &QPushButton::clicked, this, [this]() {
    if (!tree_widget_) return;
    auto &doc = tree_widget_->document();
    if (layer_index_ >= doc.layer_count()) return;
    const auto &cur_layer = doc.layer(layer_index_);

    // Parented to the tree (not this row): a deferred rebuild may run inside
    // this dialog's nested event loop and delete the row widget.
    QDialog dlg(tree_widget_, Qt::Popup);
    auto *vl = new QVBoxLayout(&dlg);
    vl->setContentsMargins(8, 8, 8, 8);
    auto *title = new QLabel(QString::fromStdString(cur_layer.name()) + tr(" (Preview)"), &dlg);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    vl->addWidget(title);
    auto *lbl = new QLabel(&dlg);
    lbl->setPixmap(render_layer_preview(cur_layer, 360, 240));
    vl->addWidget(lbl);
    dlg.exec();
  });
  layout->addWidget(preview_btn_);

  // 5. Visibility Button
  eye_btn_ = new QPushButton(this);
  eye_btn_->setFixedSize(22, 22);
  eye_btn_->setFlat(true);
  eye_btn_->setCursor(Qt::PointingHandCursor);
  eye_btn_->setToolTip(tr(layer.visible() ? "Hide Layer" : "Show Layer"));
  eye_btn_->setStyleSheet(btn_css);
  const QString eye_icon = layer.visible() ? ":/icons/icons/layer-visible-symbolic.svg"
                                          : ":/icons/icons/layer-hidden-symbolic.svg";
  eye_btn_->setIcon(IconHelper::load_symbolic(eye_icon, pal, 16));
  connect(eye_btn_, &QPushButton::clicked, this, [this]() {
    emit visibilityToggled(layer_index_);
  });
  layout->addWidget(eye_btn_);

  // Lock indicator / toggle
  if (layer.locked()) {
    lock_btn_ = new QPushButton(this);
    lock_btn_->setFixedSize(20, 20);
    lock_btn_->setFlat(true);
    lock_btn_->setCursor(Qt::PointingHandCursor);
    lock_btn_->setToolTip(tr("Locked — Click to Unlock"));
    lock_btn_->setStyleSheet(btn_css);
    lock_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/layer-lock-symbolic.svg", pal, 14));
    connect(lock_btn_, &QPushButton::clicked, this, [this]() {
      emit lockToggled(layer_index_);
    });
    layout->addWidget(lock_btn_);
  }


  // Highlight active layer
  if (is_active) {
    setStyleSheet("#LayerRowWidget { background-color: rgba(59, 130, 246, 0.28); border: 1.5px solid #3b82f6; border-radius: 6px; }");
  } else {
    setStyleSheet("#LayerRowWidget { background-color: transparent; border: 1.5px solid transparent; border-radius: 6px; } #LayerRowWidget:hover { background-color: rgba(255, 255, 255, 0.08); border-radius: 6px; }");
  }
}

QString LayerRowWidget::build_subtitle(const core::Layer &layer, int child_count) {
  QString subtitle;
  if (layer.is_group()) {
    subtitle = tr("Group • %1 layers").arg(child_count);
  } else {
    static const struct {
      const char *key;
      const char *name;
    } kOps[] = {
        {"normal", "Normal"},
        {"multiply", "Multiply"},
        {"color_burn", "Color Burn"},
        {"screen", "Screen"},
        {"add", "Add"},
        {"color_dodge", "Color Dodge"},
        {"overlay", "Overlay"},
        {"soft_light", "Soft Light"},
    };
    QString op_name = "Normal";
    for (const auto &op : kOps) {
      if (layer.composite_op() == op.key) {
        op_name = op.name;
        break;
      }
    }
    const int op_pct = static_cast<int>(std::round(layer.opacity() * 100.0));
    if (layer.clipping_mask()) {
      subtitle = QString::fromUtf8("↳ %1 • %2%").arg(op_name).arg(op_pct);
    } else {
      subtitle = QString("%1 • %2%").arg(op_name).arg(op_pct);
    }
  }
  return subtitle;
}

void LayerRowWidget::update_state(const core::Layer &layer, bool is_active, int child_count) {
  if (name_label_) {
    name_label_->setText(QString::fromStdString(layer.name()));
    QFont f = name_label_->font();
    f.setBold(layer.is_group());
    name_label_->setFont(f);
  }
  if (subtitle_label_) {
    subtitle_label_->setText(build_subtitle(layer, child_count));
  }
  if (eye_btn_) {
    const QString eye_icon = layer.visible() ? ":/icons/icons/layer-visible-symbolic.svg"
                                            : ":/icons/icons/layer-hidden-symbolic.svg";
    eye_btn_->setIcon(IconHelper::load_symbolic(eye_icon, palette(), 16));
    eye_btn_->setToolTip(tr(layer.visible() ? "Hide Layer" : "Show Layer"));
  }
  if (is_active) {
    setStyleSheet("#LayerRowWidget { background-color: rgba(59, 130, 246, 0.28); border: 1.5px solid #3b82f6; border-radius: 6px; }");
  } else {
    setStyleSheet("#LayerRowWidget { background-color: transparent; border: 1.5px solid transparent; border-radius: 6px; } #LayerRowWidget:hover { background-color: rgba(255, 255, 255, 0.08); border-radius: 6px; }");
  }
}
void LayerRowWidget::set_selected(bool sel) {
  if (sel) {
    setStyleSheet("#LayerRowWidget { background-color: rgba(59, 130, 246, 0.28); border: 1.5px solid #3b82f6; border-radius: 6px; }");
  } else {
    setStyleSheet("#LayerRowWidget { background-color: transparent; border: 1.5px solid transparent; border-radius: 6px; } #LayerRowWidget:hover { background-color: rgba(255, 255, 255, 0.08); border-radius: 6px; }");
  }
}


void LayerRowWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    drag_start_pos_ = event->pos();
  }
  emit selected(layer_index_, event->modifiers());
  QWidget::mousePressEvent(event);
}

void LayerRowWidget::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    if ((event->pos() - drag_start_pos_).manhattanLength() >= QApplication::startDragDistance()) {
      auto *drag = new QDrag(this);
      auto *mimeData = new QMimeData();
      QByteArray data;
      QDataStream stream(&data, QIODevice::WriteOnly);
      stream << static_cast<qint32>(layer_index_);
      mimeData->setData("application/x-kreart-layer", data);
      drag->setMimeData(mimeData);

      QPixmap pix = grab();
      drag->setPixmap(pix);
      drag->setHotSpot(event->pos());
      drag->exec(Qt::MoveAction);
      return;
    }
  }
  QWidget::mouseMoveEvent(event);
}

void LayerRowWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  emit renameRequested(layer_index_);
  QWidget::mouseDoubleClickEvent(event);
}

void LayerRowWidget::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat("application/x-kreart-layer")) {
    drag_over_ = true;
    event->acceptProposedAction();
    update();
  } else {
    event->ignore();
  }
}

void LayerRowWidget::update_drag_autoscroll(const QPoint &global_pos) {
  if (!tree_widget_) return;
  QScrollBar *vsb = tree_widget_->verticalScrollBar();
  if (!vsb || vsb->minimum() >= vsb->maximum()) {
    stop_drag_autoscroll();
    return;
  }
  const QPoint vp_pos = tree_widget_->viewport()->mapFromGlobal(global_pos);
  const QRect vp = tree_widget_->viewport()->rect();
  const int edge = 30;
  int speed = 0;
  if (vp_pos.y() <= edge && vp_pos.y() >= 0 && vsb->value() > vsb->minimum()) {
    speed = -((edge - vp_pos.y()) / 3 + 1);
  } else if (vp_pos.y() >= vp.height() - edge && vp_pos.y() <= vp.height() &&
             vsb->value() < vsb->maximum()) {
    speed = ((vp_pos.y() - (vp.height() - edge)) / 3 + 1);
  }
  scroll_speed_ = speed;
  if (speed != 0) {
    if (!scroll_timer_.isActive()) scroll_timer_.start(16, this);
  } else {
    scroll_timer_.stop();
  }
}

void LayerRowWidget::stop_drag_autoscroll() {
  scroll_timer_.stop();
  scroll_speed_ = 0;
}

void LayerRowWidget::dragMoveEvent(QDragMoveEvent *event) {
  if (event->mimeData()->hasFormat("application/x-kreart-layer")) {
    const double rel_y = event->position().y() / static_cast<double>(std::max(1, height()));
    const bool is_group = tree_widget_ && layer_index_ < tree_widget_->document().layer_count()
                              && tree_widget_->document().layer(layer_index_).is_group();

    if (is_group && rel_y >= 0.28 && rel_y <= 0.72) {
      drop_indicator_pos_ = 0; // inside folder
    } else {
      drop_indicator_pos_ = (rel_y < 0.5) ? -1 : 1; // -1: above, 1: below
    }
    event->acceptProposedAction();
    update();
    update_drag_autoscroll(QCursor::pos());
  } else {
    event->ignore();
  }
}

void LayerRowWidget::dragLeaveEvent(QDragLeaveEvent *) {
  drag_over_ = false;
  drop_indicator_pos_ = 0;
  stop_drag_autoscroll();
  update();
}

void LayerRowWidget::dropEvent(QDropEvent *event) {
  if (event->mimeData()->hasFormat("application/x-kreart-layer")) {
    QByteArray data = event->mimeData()->data("application/x-kreart-layer");
    QDataStream stream(&data, QIODevice::ReadOnly);
    qint32 from_idx;
    stream >> from_idx;

    const double rel_y = event->position().y() / static_cast<double>(std::max(1, height()));
    if (tree_widget_) {
      tree_widget_->perform_drop(static_cast<std::size_t>(from_idx), layer_index_, rel_y);
    }
    drag_over_ = false;
    drop_indicator_pos_ = 0;
    stop_drag_autoscroll();
    update();
    event->acceptProposedAction();
  }
}

void LayerRowWidget::timerEvent(QTimerEvent *event) {
  if (event->timerId() == scroll_timer_.timerId()) {
    if (scroll_speed_ != 0 && tree_widget_ && tree_widget_->verticalScrollBar()) {
      QScrollBar *vsb = tree_widget_->verticalScrollBar();
      const int nv = std::clamp(vsb->value() + scroll_speed_, vsb->minimum(), vsb->maximum());
      vsb->setValue(nv);
    }
  }
  QWidget::timerEvent(event);
}

void LayerRowWidget::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);
  if (drag_over_) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#3b82f6"), 2.5));
    if (drop_indicator_pos_ == -1) {
      p.drawLine(0, 1, width(), 1);
    } else if (drop_indicator_pos_ == 1) {
      p.drawLine(0, height() - 1, width(), height() - 1);
    } else {
      p.setPen(QPen(QColor("#3b82f6"), 1.5));
      p.setBrush(QColor(59, 130, 246, 35));
      p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
    }
  }
}

// ============================================================================
// LayerPanel Implementation
// ============================================================================

LayerPanel::LayerPanel(core::Document &doc, Canvas *canvas, QWidget *parent)
    : QWidget(parent), document_(doc), canvas_(canvas) {
  rebuild_timer_.setSingleShot(true);
  rebuild_timer_.setInterval(0);
  connect(&rebuild_timer_, &QTimer::timeout, this, [this]() {
    // A rebuild deletes every LayerRowWidget, which would destroy widgets a
    // nested event loop (preview popup, context menu) is still using. Defer
    // until no popup/modal is open instead.
    if (QApplication::activePopupWidget() != nullptr ||
        QApplication::activeModalWidget() != nullptr) {
      rebuild_timer_.start();
      return;
    }
    rebuild_layer_list();
  });

  setup_ui();
  rebuild_layer_list();

  if (canvas_) {
    connect(canvas_, &Canvas::documentChanged, this, [this]() {
      if (!updating_ui_) {
        schedule_rebuild();
      }
    });
    connect(canvas_, &Canvas::activeLayerAutoSelected, this, [this](std::size_t idx) {
      select_layer(idx, false);
    });
  }
}

void LayerPanel::schedule_rebuild() {
  if (!rebuild_timer_.isActive()) {
    rebuild_timer_.start();
  }
}

void LayerPanel::setup_ui() {
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(6, 6, 6, 6);
  root_layout->setSpacing(6);

  // 1. Selection Layer Header Card
  selection_card_ = new SelectionLayerCard(this);
  connect(selection_card_, &SelectionLayerCard::clicked, this, [this]() {
    if (canvas_) {
      const bool new_mode = !canvas_->selection_layer_mode();
      canvas_->set_selection_layer_mode(new_mode);
      selection_card_->set_selected(new_mode);
      if (new_mode) {
        selected_layers_.clear();
        tree_widget_->clearSelection();
        rebuild_layer_list();
      } else {
        if (document_.has_active_layer()) {
          selected_layers_.insert(document_.active_layer_index());
        }
        rebuild_layer_list();
      }
    }
  });
  connect(selection_card_, &SelectionLayerCard::previewClicked, this, [this]() {
    QDialog dlg(tree_widget_, Qt::Popup);
    auto *vl = new QVBoxLayout(&dlg);
    vl->setContentsMargins(8, 8, 8, 8);
    auto *title = new QLabel(tr("Selection (Preview)"), &dlg);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    vl->addWidget(title);
    auto *lbl = new QLabel(&dlg);
    lbl->setPixmap(render_selection_preview(document_, canvas_, 360, 240));
    vl->addWidget(lbl);
    dlg.exec();
  });
  root_layout->addWidget(selection_card_);

  // 2. Search edit
  search_edit_ = new QLineEdit(this);
  search_edit_->setPlaceholderText(tr("Search layers..."));
  search_edit_->setClearButtonEnabled(true);
  connect(search_edit_, &QLineEdit::textChanged, this, [this](const QString &) {
    rebuild_layer_list();
  });
  root_layout->addWidget(search_edit_);

  // 3. Tree Widget
  tree_widget_ = new LayerTreeWidget(document_, canvas_, this);
  tree_widget_->set_rebuild_callback([this]() {
    rebuild_layer_list();
  });
  connect(tree_widget_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
    if (updating_ui_ || !cur || !document_.has_active_layer()) return;
    const int idx = cur->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(document_.layer_count())) {
      document_.set_active_layer_index(idx);
      selected_layers_.clear();
      selected_layers_.insert(idx);
      if (canvas_ && canvas_->selection_layer_mode()) {
        canvas_->set_selection_layer_mode(false);
        if (selection_card_) selection_card_->set_selected(false);
      }
      update_controls();
      emit activeLayerChanged(idx);
    }
  });
  connect(tree_widget_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
    if (updating_ui_ || !item) return;
    const int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(document_.layer_count())) {
      document_.layer(idx).set_expanded(true);
      rebuild_layer_list();
    }
  });
  connect(tree_widget_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
    if (updating_ui_ || !item) return;
    const int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(document_.layer_count())) {
      document_.layer(idx).set_expanded(false);
      rebuild_layer_list();
    }
  });
  tree_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tree_widget_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
    QTreeWidgetItem *item = tree_widget_->itemAt(pos);
    if (item) {
      const int idx = item->data(0, Qt::UserRole).toInt();
      show_context_menu(idx, tree_widget_->viewport()->mapToGlobal(pos));
    }
  });
  root_layout->addWidget(tree_widget_, 1);

  // 4. Bottom Controls: Opacity row
  auto *opacity_row = new QHBoxLayout();
  opacity_row->setSpacing(4);

  opacity_dec_btn_ = new QPushButton("-", this);
  opacity_dec_btn_->setFixedSize(24, 24);
  connect(opacity_dec_btn_, &QPushButton::clicked, this, [this]() {
    if (opacity_slider_) {
      opacity_slider_->setValue(std::max(0, opacity_slider_->value() - 1));
    }
  });
  opacity_row->addWidget(opacity_dec_btn_);

  opacity_slider_ = new QSlider(Qt::Horizontal, this);
  opacity_slider_->setRange(0, 100);
  opacity_slider_->setValue(100);
  connect(opacity_slider_, &QSlider::valueChanged, this, &LayerPanel::on_opacity_slider_changed);
  connect(opacity_slider_, &QSlider::sliderPressed, this, [this]() {
    document_.begin_change();
    opacity_drag_active_ = true;
    opacity_dirty_ = false;
  });
  connect(opacity_slider_, &QSlider::sliderReleased, this, [this]() {
    if (opacity_dirty_) document_.commit_change();
    opacity_drag_active_ = false;
    opacity_dirty_ = false;
  });
  opacity_row->addWidget(opacity_slider_, 1);

  opacity_inc_btn_ = new QPushButton("+", this);
  opacity_inc_btn_->setFixedSize(24, 24);
  connect(opacity_inc_btn_, &QPushButton::clicked, this, [this]() {
    if (opacity_slider_) {
      opacity_slider_->setValue(std::min(100, opacity_slider_->value() + 1));
    }
  });
  opacity_row->addWidget(opacity_inc_btn_);

  opacity_label_ = new QLabel("100%", this);
  opacity_label_->setFixedWidth(40);
  opacity_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  opacity_row->addWidget(opacity_label_);
  root_layout->addLayout(opacity_row);

  // 5. Mode & Adjustment Row
  auto *adj_row = new QHBoxLayout();
  adj_row->setSpacing(4);

  const auto &pal = palette();

  clipping_btn_ = new QPushButton(this);
  clipping_btn_->setCheckable(true);
  clipping_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/layer-clipping-symbolic.svg", pal, 16));
  clipping_btn_->setToolTip(tr("Toggle Clipping Mask (Ctrl+Alt+G)"));
  clipping_btn_->setFixedSize(28, 28);
  connect(clipping_btn_, &QPushButton::clicked, this, &LayerPanel::on_toggle_clipping_mask);
  adj_row->addWidget(clipping_btn_);

  alpha_lock_btn_ = new QPushButton(this);
  alpha_lock_btn_->setCheckable(true);
  alpha_lock_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/layer-alpha-symbolic.svg", pal, 16));
  alpha_lock_btn_->setToolTip(tr("Toggle Alpha Lock"));
  alpha_lock_btn_->setFixedSize(28, 28);
  connect(alpha_lock_btn_, &QPushButton::clicked, this, &LayerPanel::on_toggle_alpha_lock);
  adj_row->addWidget(alpha_lock_btn_);

  blend_mode_combo_ = new QComboBox(this);
  blend_mode_combo_->addItems({tr("Normal"), tr("Multiply"), tr("Color Burn"), tr("Screen"),
                               tr("Add / Glow"), tr("Color Dodge"), tr("Overlay"), tr("Soft Light")});
  connect(blend_mode_combo_, &QComboBox::currentIndexChanged, this, &LayerPanel::on_blend_mode_changed);
  adj_row->addWidget(blend_mode_combo_, 1);

  screentone_combo_ = new QComboBox(this);
  screentone_combo_->addItems({tr("Tone: Off"), tr("Dot"), tr("Line"), tr("Hatch")});
  connect(screentone_combo_, &QComboBox::currentIndexChanged, this, &LayerPanel::on_screentone_changed);
  adj_row->addWidget(screentone_combo_);

  root_layout->addLayout(adj_row);

  // 6. Action Toolbar
  auto *btn_bar = new QHBoxLayout();
  btn_bar->setSpacing(3);

  add_layer_btn_ = new QPushButton(this);
  add_layer_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/list-add-symbolic.svg", pal, 16));
  add_layer_btn_->setToolTip(tr("Add Layer"));
  connect(add_layer_btn_, &QPushButton::clicked, this, &LayerPanel::on_add_layer);
  btn_bar->addWidget(add_layer_btn_);

  add_group_btn_ = new QPushButton(this);
  add_group_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/folder-symbolic.svg", pal, 16));
  add_group_btn_->setToolTip(tr("Add Group"));
  connect(add_group_btn_, &QPushButton::clicked, this, &LayerPanel::on_add_group);
  btn_bar->addWidget(add_group_btn_);

  import_btn_ = new QPushButton(this);
  import_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/camera-symbolic.svg", pal, 16));
  import_btn_->setToolTip(tr("Import Image as Layer"));
  connect(import_btn_, &QPushButton::clicked, this, &LayerPanel::on_import_image_clicked);
  btn_bar->addWidget(import_btn_);

  duplicate_btn_ = new QPushButton(this);
  duplicate_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/view-grid-symbolic.svg", pal, 16));
  duplicate_btn_->setToolTip(tr("Duplicate Layer"));
  connect(duplicate_btn_, &QPushButton::clicked, this, &LayerPanel::on_duplicate_layer);
  btn_bar->addWidget(duplicate_btn_);

  merge_down_btn_ = new QPushButton(this);
  merge_down_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/layer-merge-down-symbolic.svg", pal, 16));
  merge_down_btn_->setToolTip(tr("Merge Down"));
  connect(merge_down_btn_, &QPushButton::clicked, this, &LayerPanel::on_merge_down);
  btn_bar->addWidget(merge_down_btn_);

  flip_h_btn_ = new QPushButton(this);
  flip_h_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/symmetry-h-symbolic.svg", pal, 16));
  flip_h_btn_->setToolTip(tr("Flip Horizontally"));
  connect(flip_h_btn_, &QPushButton::clicked, this, &LayerPanel::on_flip_horizontal);
  btn_bar->addWidget(flip_h_btn_);

  flip_v_btn_ = new QPushButton(this);
  flip_v_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/symmetry-v-symbolic.svg", pal, 16));
  flip_v_btn_->setToolTip(tr("Flip Vertically"));
  connect(flip_v_btn_, &QPushButton::clicked, this, &LayerPanel::on_flip_vertical);
  btn_bar->addWidget(flip_v_btn_);

  move_up_btn_ = new QPushButton(this);
  move_up_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/go-up-symbolic.svg", pal, 16));
  move_up_btn_->setToolTip(tr("Move Layer Up"));
  connect(move_up_btn_, &QPushButton::clicked, this, &LayerPanel::on_move_layer_up);
  btn_bar->addWidget(move_up_btn_);

  move_down_btn_ = new QPushButton(this);
  move_down_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/go-down-symbolic.svg", pal, 16));
  move_down_btn_->setToolTip(tr("Move Layer Down"));
  connect(move_down_btn_, &QPushButton::clicked, this, &LayerPanel::on_move_layer_down);
  btn_bar->addWidget(move_down_btn_);
  delete_btn_ = new QPushButton(this);
  delete_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/list-remove-symbolic.svg", pal, 16));
  delete_btn_->setToolTip(tr("Delete Layer"));
  connect(delete_btn_, &QPushButton::clicked, this, &LayerPanel::on_delete_layer);
  btn_bar->addWidget(delete_btn_);

  root_layout->addLayout(btn_bar);
}

void LayerPanel::rebuild_layer_list() {
  if (!tree_widget_) return;
  updating_ui_ = true;
  // Preserving scroll position: clear() resets it to 0, which made operations like
  // clipping / adding / importing / deleting jump the list back to the top.
  const int saved_scroll = tree_widget_->verticalScrollBar()->value();
  tree_widget_->clear();
  QScrollBar *saved_bar = tree_widget_->verticalScrollBar();

  const bool sel_mode = canvas_ && canvas_->selection_layer_mode();
  if (selection_card_) {
    selection_card_->set_selected(sel_mode);
  }

  // Enforce single selection: exactly the active layer is selected when active.
  selected_layers_.clear();
  if (document_.has_active_layer() && !sel_mode && document_.active_layer_index() < document_.layer_count()) {
    selected_layers_.insert(document_.active_layer_index());
  }

  const auto &pal = palette();
  const QString search = search_edit_ ? search_edit_->text().toLower() : "";
  const std::size_t active_idx = document_.active_layer_index();
  std::unordered_map<std::size_t, QTreeWidgetItem *> id_to_item;
  std::unordered_map<std::size_t, int> group_child_counts;
  for (std::size_t i = 0; i < document_.layer_count(); ++i) {
    if (document_.layer(i).parent_id() != 0) {
      group_child_counts[document_.layer(i).parent_id()]++;
    }
  }

  std::vector<std::pair<std::size_t, QTreeWidgetItem *>> ordered_items;

  for (std::size_t i = document_.layer_count(); i > 0; --i) {
    const std::size_t idx = i - 1;
    const auto &layer = document_.layer(idx);

    auto *item = new QTreeWidgetItem();
    id_to_item[layer.id()] = item;
    ordered_items.emplace_back(idx, item);

    // Match the QTreeWidgetItem slot height to the 42px LayerRowWidget (plus 4px gap between
    // rows); without this hint the item sits ~24px tall and the row widget overflows its slot,
    // which used to break highlights and inject phantom gaps into the tree.
    item->setSizeHint(0, QSize(1, 46));
    item->setSizeHint(1, QSize(1, 46));

    item->setData(0, Qt::UserRole, static_cast<int>(idx));
    item->setData(0, Qt::UserRole + 1, layer.is_group());
    item->setData(1, Qt::UserRole, static_cast<int>(idx));
    item->setData(2, Qt::UserRole, static_cast<int>(idx));
    item->setData(3, Qt::UserRole, static_cast<int>(idx));

    // Visibility in col 0 for compatibility
    if (layer.visible()) {
      item->setIcon(0, IconHelper::load_symbolic(":/icons/icons/layer-visible-symbolic.svg", pal, 16));
    } else {
      item->setIcon(0, IconHelper::load_symbolic(":/icons/icons/layer-hidden-symbolic.svg", pal, 16));
    }

    // Name in col 1 for compatibility with test assertions
    // NOTE: never set an icon on column 1 — QTreeView insets persistent item widgets
    // (setItemWidget) by the column decoration size, which shifts every row horizontally
    // and shows a dead strip on the left of the tree. The row widget draws its own icon.
    QString name = QString::fromStdString(layer.name());
    if (layer.clipping_mask()) {
      name = QString::fromUtf8("↳ ") + name;
    }
    item->setText(1, name);
    if (layer.is_group()) {
      QFont font = item->font(1);
      font.setBold(true);
      item->setFont(1, font);
      item->setExpanded(layer.expanded());
    }

    // Clipping in col 2 for compatibility with test assertions
    if (layer.clipping_mask()) {
      item->setIcon(2, IconHelper::load_symbolic(":/icons/icons/layer-clipping-symbolic.svg", pal, 16));
    }

    // Lock in col 3 for compatibility
    if (layer.locked()) {
      item->setIcon(3, IconHelper::load_symbolic(":/icons/icons/layer-lock-symbolic.svg", pal, 16));
    }

    // Hide non-matching rows, but keep a group visible when any direct child matches
    bool search_hidden = !search.isEmpty() && !QString::fromStdString(layer.name()).toLower().contains(search);
    if (search_hidden && layer.is_group()) {
      const std::size_t gid = layer.id();
      for (std::size_t d = 0; d < document_.layer_count(); ++d) {
        if (document_.layer(d).parent_id() == gid &&
            QString::fromStdString(document_.layer(d).name()).toLower().contains(search)) {
          search_hidden = false;
          break;
        }
      }
    }
    if (search_hidden) {
      item->setHidden(true);
    }
  }

  // Attach to parent group or tree root preserving order
  for (const auto &[idx, item] : ordered_items) {
    const auto &layer = document_.layer(idx);
    if (layer.parent_id() != 0 && id_to_item.count(layer.parent_id())) {
      id_to_item[layer.parent_id()]->addChild(item);
    } else {
      tree_widget_->addTopLevelItem(item);
    }

    const bool is_active = !sel_mode && document_.has_active_layer() && (idx == active_idx);
    const int child_count = layer.is_group() ? group_child_counts[layer.id()] : 0;
    const int depth = document_.get_layer_depth(idx);
    auto *row_widget = new LayerRowWidget(idx, layer, is_active, child_count, depth, tree_widget_, tree_widget_);

    connect(row_widget, &LayerRowWidget::selected, this, [this](std::size_t layer_i, Qt::KeyboardModifiers) {
      select_layer(layer_i);
    });
    connect(row_widget, &LayerRowWidget::visibilityToggled, this, [this](std::size_t layer_i) {
      if (layer_i < document_.layer_count()) {
        document_.set_layer_visible(layer_i, !document_.layer(layer_i).visible());
        if (canvas_) canvas_->refresh();
        rebuild_layer_list();
        emit layerModified();
      }
    });
    connect(row_widget, &LayerRowWidget::expandToggled, this, [this](std::size_t layer_i) {
      if (layer_i < document_.layer_count()) {
        document_.layer(layer_i).set_expanded(!document_.layer(layer_i).expanded());
        rebuild_layer_list();
      }
    });
    connect(row_widget, &LayerRowWidget::lockToggled, this, [this](std::size_t layer_i) {
      if (layer_i < document_.layer_count()) {
        document_.layer(layer_i).set_locked(!document_.layer(layer_i).locked());
        if (canvas_) canvas_->refresh();
        rebuild_layer_list();
        emit layerModified();
      }
    });
    connect(row_widget, &LayerRowWidget::renameRequested, this, &LayerPanel::on_rename_layer);

    tree_widget_->setItemWidget(item, 1, row_widget);

    if (is_active && !sel_mode) {
      tree_widget_->setCurrentItem(item);
    }
  }
  for (const auto &[idx, item] : ordered_items) {
    const auto &layer = document_.layer(idx);
    if (layer.is_group()) {
      item->setExpanded(layer.expanded());
    }
    const bool is_collapsed = document_.is_ancestor_collapsed(idx);
    item->setHidden(is_collapsed);
    auto *rw = qobject_cast<LayerRowWidget *>(tree_widget_->itemWidget(item, 1));
    if (rw) {
      rw->setVisible(!is_collapsed);
    }
  }


  if (!document_.has_active_layer() || sel_mode) {
    tree_widget_->setCurrentItem(nullptr);
    tree_widget_->clearSelection();
  }

  updating_ui_ = false;
  update_controls();

  // Restore the scroll position after the new layout has been laid out
  if (saved_bar) {
    QTimer::singleShot(0, saved_bar, [saved_bar, saved_scroll]() {
      saved_bar->setValue(std::min(saved_scroll, saved_bar->maximum()));
    });
  }
}

void LayerPanel::select_layer(std::size_t index, bool /*multi*/) {
  if (canvas_ && canvas_->selection_layer_mode()) {
    canvas_->set_selection_layer_mode(false);
    if (selection_card_) selection_card_->set_selected(false);
  }
  if (index >= document_.layer_count()) return;

  selected_layers_.clear();
  selected_layers_.insert(index);
  document_.set_active_layer_index(index);
  update_controls();
  update_row_selection_highlights();
  if (document_.has_active_layer()) {
    emit activeLayerChanged(document_.active_layer_index());
  }
}

void LayerPanel::clear_selection() {
  selected_layers_.clear();
  document_.clear_active_layer();
  if (tree_widget_) {
    tree_widget_->setCurrentItem(nullptr);
    tree_widget_->clearSelection();
  }
  update_controls();
  update_row_selection_highlights();
}

void LayerPanel::sync_selection_to_active() {
  selected_layers_.clear();
  if (document_.has_active_layer()) {
    selected_layers_.insert(document_.active_layer_index());
  }
}
void LayerPanel::update_row_selection_highlights() {
  const std::size_t active_idx = document_.active_layer_index();
  const bool has_act = document_.has_active_layer();
  for (int i = 0; i < tree_widget_->topLevelItemCount(); ++i) {
    update_item_highlight_recursive(tree_widget_->topLevelItem(i), active_idx, has_act);
  }
}

void LayerPanel::update_item_highlight_recursive(QTreeWidgetItem *item, std::size_t active_idx, bool has_act) {
  if (!item) return;
  auto *w = qobject_cast<LayerRowWidget *>(tree_widget_->itemWidget(item, 1));
  if (w) {
    const std::size_t idx = w->layer_index();
    const bool is_sel = has_act && (idx == active_idx);
    w->set_selected(is_sel);
  }
  for (int c = 0; c < item->childCount(); ++c) {
    update_item_highlight_recursive(item->child(c), active_idx, has_act);
  }
}


void LayerPanel::update_controls() {
  if (selection_card_) {
    const bool has_sel = document_.selection().active();
    const int sw = has_sel ? document_.selection().width() : 0;
    const int sh = has_sel ? document_.selection().height() : 0;
    selection_card_->update_status(has_sel, sw, sh);
  }

  if (!document_.has_active_layer() || document_.layer_count() == 0) {
    if (opacity_slider_) {
      opacity_slider_->blockSignals(true);
      opacity_slider_->setValue(100);
      opacity_slider_->blockSignals(false);
    }
    if (opacity_label_) opacity_label_->setText("100%");
    if (clipping_btn_) clipping_btn_->setStyleSheet("");
    if (alpha_lock_btn_) alpha_lock_btn_->setStyleSheet("");
    return;
  }

  const std::size_t active_idx = document_.active_layer_index();
  if (active_idx >= document_.layer_count()) return;

  const auto &active = document_.active_layer();

  if (opacity_slider_) {
    const int op_val = static_cast<int>(std::round(active.opacity() * 100.0));
    opacity_slider_->blockSignals(true);
    opacity_slider_->setValue(op_val);
    opacity_slider_->blockSignals(false);
    if (opacity_label_) {
      opacity_label_->setText(QString::number(op_val) + "%");
    }
  }

  if (blend_mode_combo_) {
    static const char *kOps[] = {"normal", "multiply", "color_burn", "screen", "add", "color_dodge", "overlay", "soft_light"};
    blend_mode_combo_->blockSignals(true);
    for (int b = 0; b < 8; ++b) {
      if (active.composite_op() == kOps[b]) {
        blend_mode_combo_->setCurrentIndex(b);
        break;
      }
    }
    blend_mode_combo_->blockSignals(false);
  }

  if (screentone_combo_) {
    screentone_combo_->blockSignals(true);
    screentone_combo_->setCurrentIndex(active.screentone() ? 1 : 0);
    screentone_combo_->blockSignals(false);
  }

  if (clipping_btn_) {
    clipping_btn_->blockSignals(true);
    clipping_btn_->setChecked(active.clipping_mask());
    clipping_btn_->blockSignals(false);
  }

  if (alpha_lock_btn_) {
    alpha_lock_btn_->blockSignals(true);
    alpha_lock_btn_->setChecked(active.alpha_locked());
    alpha_lock_btn_->blockSignals(false);
  }
}

void LayerPanel::on_opacity_slider_changed(int val) {
  if (updating_ui_) return;
  const std::size_t idx = document_.active_layer_index();
  if (idx < document_.layer_count()) {
    if (!opacity_drag_active_) document_.begin_change();
    document_.set_layer_opacity(idx, val / 100.0);
    if (opacity_drag_active_) opacity_dirty_ = true;
    else document_.commit_change();
    if (opacity_label_) {
      opacity_label_->setText(QString::number(val) + "%");
    }
    // Live-update this row's subtitle (Normal • NN%) without a full rebuild.
    if (tree_widget_) {
      std::function<void(QTreeWidgetItem *)> refresh = [&](QTreeWidgetItem *it) {
        if (!it) return;
        auto *rw = qobject_cast<LayerRowWidget *>(tree_widget_->itemWidget(it, 1));
        if (rw && rw->layer_index() == idx) {
          const auto &layer = document_.layer(idx);
          int child_count = 0;
          if (layer.is_group()) {
            QTreeWidgetItem *parent = it;
            for (int c = 0; c < parent->childCount(); ++c) {
              auto *cw = qobject_cast<LayerRowWidget *>(tree_widget_->itemWidget(parent->child(c), 1));
              if (cw && document_.layer(cw->layer_index()).parent_id() == idx) ++child_count;
            }
          }
          rw->update_state(layer, true, child_count);
          return; // layer indices are unique; no need to keep scanning
        }
        for (int c = 0; c < it->childCount(); ++c) refresh(it->child(c));
      };
      for (int i = 0; i < tree_widget_->topLevelItemCount(); ++i) {
        refresh(tree_widget_->topLevelItem(i));
      }
    }
    if (canvas_) canvas_->refresh();
    emit layerModified();
  }
}

void LayerPanel::on_blend_mode_changed(int idx) {
  if (updating_ui_) return;
  const std::size_t l_idx = document_.active_layer_index();
  if (l_idx >= document_.layer_count()) return;

  static const char *kOps[] = {"normal", "multiply", "color_burn", "screen", "add", "color_dodge", "overlay", "soft_light"};
  if (idx >= 0 && idx < 8) {
    document_.begin_change();
    document_.set_layer_composite_op(l_idx, kOps[idx]);
    document_.commit_change();
    if (canvas_) canvas_->refresh();
    rebuild_layer_list();
    emit layerModified();
  }
}

void LayerPanel::on_screentone_changed(int idx) {
  if (updating_ui_) return;
  const std::size_t l_idx = document_.active_layer_index();
  if (l_idx >= document_.layer_count()) return;

  document_.begin_change();
  if (idx == 0) {
    document_.set_layer_screentone(l_idx, false);
  } else {
    document_.set_layer_screentone(l_idx, true);
    if (idx == 2) {
      document_.set_layer_screentone_pattern(l_idx, core::ScreentonePattern::LINE_45);
    } else if (idx == 3) {
      document_.set_layer_screentone_pattern(l_idx, core::ScreentonePattern::CROSSHATCH);
    } else {
      document_.set_layer_screentone_pattern(l_idx, core::ScreentonePattern::DOT_60);
    }
  }
  document_.commit_change();
  if (canvas_) canvas_->refresh();
  emit layerModified();
}

void LayerPanel::on_add_layer() {
  if (canvas_ && canvas_->selection_layer_mode()) {
    canvas_->set_selection_layer_mode(false);
    if (selection_card_) selection_card_->set_selected(false);
  }
  const std::size_t active_idx = document_.active_layer_index();
  document_.begin_change();
  if (document_.has_active_layer() && active_idx < document_.layer_count()) {
    const auto &active = document_.layer(active_idx);
    if (active.is_group()) {
      const std::size_t new_idx = document_.add_layer("Layer " + std::to_string(document_.layer_count() + 1));
      document_.move_into_group(new_idx, active_idx);
    } else {
      const std::size_t new_idx = document_.add_layer("Layer " + std::to_string(document_.layer_count() + 1));
      document_.layer(new_idx).set_parent_id(active.parent_id());
      document_.clean_hierarchy_invariants();
    }
  } else {
    document_.add_layer("Layer " + std::to_string(document_.layer_count() + 1));
  }
  document_.commit_change();
  sync_selection_to_active();
  rebuild_layer_list();
  if (canvas_) canvas_->refresh();
  if (document_.has_active_layer()) {
    emit activeLayerChanged(document_.active_layer_index());
  }
  emit layerModified();
}

void LayerPanel::on_add_group() {
  if (canvas_ && canvas_->selection_layer_mode()) {
    canvas_->set_selection_layer_mode(false);
    if (selection_card_) selection_card_->set_selected(false);
  }
  const std::size_t active_idx = document_.active_layer_index();
  document_.begin_change();
  if (document_.has_active_layer() && active_idx < document_.layer_count()) {
    const auto &active = document_.layer(active_idx);
    if (active.is_group()) {
      const std::size_t new_grp = document_.add_group("Group " + std::to_string(document_.layer_count() + 1));
      document_.move_into_group(new_grp, active_idx);
    } else {
      const std::size_t new_grp = document_.add_group("Group " + std::to_string(document_.layer_count() + 1));
      document_.layer(new_grp).set_parent_id(active.parent_id());
      document_.clean_hierarchy_invariants();
    }
  } else {
    document_.add_group("Group " + std::to_string(document_.layer_count() + 1));
  }
  document_.commit_change();
  sync_selection_to_active();
  rebuild_layer_list();
  if (canvas_) canvas_->refresh();
  if (document_.has_active_layer()) {
    emit activeLayerChanged(document_.active_layer_index());
  }
  emit layerModified();
}


void LayerPanel::on_duplicate_layer() {
  const std::size_t idx = document_.active_layer_index();
  if (idx < document_.layer_count()) {
    document_.begin_change();
    document_.duplicate_layer(idx);
    document_.commit_change();
    sync_selection_to_active();
    rebuild_layer_list();
    if (canvas_) canvas_->refresh();
    emit layerModified();
  }
}

void LayerPanel::on_merge_down() {
  const std::size_t idx = document_.active_layer_index();
  if (idx > 0 && idx < document_.layer_count()) {
    document_.merge_layer_down(idx);
    sync_selection_to_active();
    rebuild_layer_list();
    if (canvas_) canvas_->refresh();
    emit layerModified();
  }
}

void LayerPanel::on_delete_layer() {
  const std::size_t idx = document_.active_layer_index();
  if (idx >= document_.layer_count()) return;
  const auto &layer = document_.layer(idx);
  if (layer.is_group()) {
    std::size_t l_count = 0, f_count = 0;
    document_.get_group_descendants_count(idx, l_count, f_count);
    if (l_count > 0 || f_count > 0) {
      const auto res = QMessageBox::question(
          this, tr("Delete Group"),
          tr("Group '%1' contains %2 layer(s). Delete everything inside?")
              .arg(QString::fromStdString(layer.name()))
              .arg(l_count + f_count),
          QMessageBox::Yes | QMessageBox::No);
      if (res != QMessageBox::Yes) return;
      document_.remove_group_recursive(idx);
    } else {
      document_.remove_layer(idx);
    }
  } else {
    document_.begin_change();
    document_.remove_layer(idx);
    document_.commit_change();
  }
  sync_selection_to_active();
  rebuild_layer_list();
  if (canvas_) canvas_->refresh();
  emit layerModified();
}

void LayerPanel::on_toggle_clipping_mask() {
  const std::size_t idx = document_.active_layer_index();
  if (idx < document_.layer_count()) {
    document_.begin_change();
    document_.set_layer_clipping_mask(idx, !document_.layer(idx).clipping_mask());
    document_.commit_change();
    if (canvas_) canvas_->refresh();
    rebuild_layer_list();
    emit layerModified();
  }
}

void LayerPanel::on_toggle_alpha_lock() {
  const std::size_t idx = document_.active_layer_index();
  if (idx < document_.layer_count()) {
    document_.begin_change();
    document_.set_layer_alpha_locked(idx, !document_.layer(idx).alpha_locked());
    document_.commit_change();
    if (canvas_) canvas_->refresh();
    rebuild_layer_list();
    emit layerModified();
  }
}

void LayerPanel::on_toggle_layer_lock() {
  const std::size_t idx = document_.active_layer_index();
  if (idx < document_.layer_count()) {
    document_.begin_change();
    document_.layer(idx).set_locked(!document_.layer(idx).locked());
    document_.commit_change();
    if (canvas_) canvas_->refresh();
    rebuild_layer_list();
    emit layerModified();
  }
}

void LayerPanel::on_rename_layer(std::size_t index) {
  const std::size_t idx = (index == static_cast<std::size_t>(-1)) ? document_.active_layer_index() : index;
  if (idx >= document_.layer_count()) return;
  bool ok = false;
  const QString current_name = QString::fromStdString(document_.layer(idx).name());
  const QString new_name = QInputDialog::getText(
      this, tr("Rename Layer"), tr("New name:"), QLineEdit::Normal, current_name, &ok);
  if (ok && !new_name.trimmed().isEmpty()) {
    document_.begin_change();
    document_.rename_layer(idx, new_name.trimmed().toStdString());
    document_.commit_change();
    rebuild_layer_list();
    emit layerModified();
  }
}

bool LayerPanel::import_image(const std::filesystem::path &path) {
  QImage img(QString::fromStdString(path.string()));
  if (img.isNull()) return false;
  img = img.convertToFormat(QImage::Format_ARGB32);
  const int img_w = img.width();
  const int img_h = img.height();

  document_.begin_change();
  std::string layer_name = path.stem().string();
  if (layer_name.empty()) layer_name = "Imported Image";
  const std::size_t new_idx = document_.add_layer(layer_name, false);

  const int offset_x = (document_.width() - img_w) / 2;
  const int offset_y = (document_.height() - img_h) / 2;

  auto &layer_img = document_.layer(new_idx).image();
  for (int y = 0; y < img_h; ++y) {
    const int doc_y = offset_y + y;
    if (doc_y < 0 || doc_y >= document_.height()) continue;
    const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
    for (int x = 0; x < img_w; ++x) {
      const int doc_x = offset_x + x;
      if (doc_x < 0 || doc_x >= document_.width()) continue;
      const QRgb px = line[x];
      const uint8_t a = qAlpha(px);
      if (a > 0) {
        layer_img.set_pixel(doc_x, doc_y, core::Rgba8{static_cast<uint8_t>(qRed(px)),
                                                      static_cast<uint8_t>(qGreen(px)),
                                                      static_cast<uint8_t>(qBlue(px)), a});
      }
    }
  }

  document_.set_active_layer_index(new_idx);
  document_.commit_change();
  if (canvas_) canvas_->refresh();
  rebuild_layer_list();
  emit layerModified();
  return true;
}

void LayerPanel::on_import_image_clicked() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Import Image as Layer"), QString(), tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
  if (!filename.isEmpty()) {
    import_image(filename.toStdString());
  }
}

void LayerPanel::on_flip_horizontal() {
  const std::size_t idx = document_.active_layer_index();
  if (idx >= document_.layer_count()) return;
  auto &layer = document_.layer(idx);
  if (layer.is_group()) return;

  document_.begin_change();
  const int w = layer.image().width();
  const int h = layer.image().height();
  core::TiledImage new_img(w, h);
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
        if (c.a > 0) {
          new_img.set_pixel(w - 1 - doc_x, doc_y, c);
        }
      }
    }
  }
  layer.image() = std::move(new_img);
  document_.mark_composite_dirty();
  document_.commit_change();
  if (canvas_) canvas_->refresh();
  rebuild_layer_list();
  emit layerModified();
}

void LayerPanel::on_flip_vertical() {
  const std::size_t idx = document_.active_layer_index();
  if (idx >= document_.layer_count()) return;
  auto &layer = document_.layer(idx);
  if (layer.is_group()) return;

  document_.begin_change();
  const int w = layer.image().width();
  const int h = layer.image().height();
  core::TiledImage new_img(w, h);
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
        if (c.a > 0) {
          new_img.set_pixel(doc_x, h - 1 - doc_y, c);
        }
      }
    }
  }
  layer.image() = std::move(new_img);
  document_.mark_composite_dirty();
  document_.commit_change();
  if (canvas_) canvas_->refresh();
  rebuild_layer_list();
  emit layerModified();
}
void LayerPanel::on_move_layer_up() {
  const std::size_t idx = document_.active_layer_index();
  if (idx + 1 < document_.layer_count()) {
    document_.begin_change();
    document_.move_layer(idx, idx + 1);
    document_.clean_hierarchy_invariants();
    document_.commit_change();
    sync_selection_to_active();
    if (canvas_) canvas_->refresh();
    rebuild_layer_list();
    emit layerModified();
  }
}

void LayerPanel::on_move_layer_down() {
  const std::size_t idx = document_.active_layer_index();
  if (idx > 0 && idx < document_.layer_count()) {
    document_.begin_change();
    document_.move_layer(idx, idx - 1);
    document_.clean_hierarchy_invariants();
    document_.commit_change();
    sync_selection_to_active();
    if (canvas_) canvas_->refresh();
    rebuild_layer_list();
    emit layerModified();
  }
}


void LayerPanel::show_context_menu(std::size_t index, const QPoint &global_pos) {
  if (index >= document_.layer_count()) return;
  document_.set_active_layer_index(index);
  update_controls();

  const auto &pal = palette();
  auto *menu = new QMenu(this);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/list-add-symbolic.svg", pal, 16),
                  tr("New Layer"), this, &LayerPanel::on_add_layer);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/folder-symbolic.svg", pal, 16),
                  tr("New Group"), this, &LayerPanel::on_add_group);
  menu->addSeparator();
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/layer-clipping-symbolic.svg", pal, 16),
                  tr("Toggle Clipping Mask"), this, &LayerPanel::on_toggle_clipping_mask);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/layer-alpha-symbolic.svg", pal, 16),
                  tr("Toggle Alpha Lock"), this, &LayerPanel::on_toggle_alpha_lock);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/layer-lock-symbolic.svg", pal, 16),
                  tr("Toggle Lock"), this, &LayerPanel::on_toggle_layer_lock);
  menu->addSeparator();
  menu->addAction(tr("Duplicate Layer"), this, &LayerPanel::on_duplicate_layer);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/layer-merge-down-symbolic.svg", pal, 16),
                  tr("Merge Down"), this, &LayerPanel::on_merge_down);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/symmetry-h-symbolic.svg", pal, 16),
                  tr("Flip Horizontally"), this, &LayerPanel::on_flip_horizontal);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/symmetry-v-symbolic.svg", pal, 16),
                  tr("Flip Vertically"), this, &LayerPanel::on_flip_vertical);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/go-up-symbolic.svg", pal, 16),
                  tr("Move Up"), this, &LayerPanel::on_move_layer_up);
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/go-down-symbolic.svg", pal, 16),
                  tr("Move Down"), this, &LayerPanel::on_move_layer_down);
  menu->addSeparator();
  menu->addAction(tr("Rename Layer..."), this, [this, index]() { on_rename_layer(index); });
  menu->addAction(IconHelper::load_symbolic(":/icons/icons/list-remove-symbolic.svg", pal, 16),
                  tr("Delete Layer"), this, &LayerPanel::on_delete_layer);
  menu->exec(global_pos);
}

} // namespace kreart::ui
