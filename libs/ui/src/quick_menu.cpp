#include "kreart/ui/quick_menu.h"
#include "kreart/ui/icon_helper.h"

#include <QPainter>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <cmath>

namespace kreart::ui {

QuickMenu::QuickMenu(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
  setAttribute(Qt::WA_TranslucentBackground);
  setFixedSize(300, 360);
  build_ui();
}

void QuickMenu::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);

  // Floating rounded HUD card styled from theme palette
  QColor bg = palette().color(QPalette::Window);
  bg.setAlpha(245);
  QColor border = palette().color(QPalette::Mid);
  border.setAlpha(180);

  p.setPen(QPen(border, 1.2));
  p.setBrush(bg);
  p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12.0, 12.0);
}

void QuickMenu::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Q) {
    hide();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void QuickMenu::popup_at(const QPoint &global_pos) {
  // Center HUD near the stylus tip within screen bounds
  QPoint target_pos = global_pos - QPoint(width() / 2, height() / 2);
  if (QScreen *screen = QGuiApplication::screenAt(global_pos)) {
    const QRect geom = screen->availableGeometry();
    target_pos.setX(std::clamp(target_pos.x(), geom.left() + 10, geom.right() - width() - 10));
    target_pos.setY(std::clamp(target_pos.y(), geom.top() + 10, geom.bottom() - height() - 10));
  }
  move(target_pos);
  show();
  raise();
  activateWindow();
}

void QuickMenu::build_ui() {
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(12, 12, 12, 12);
  root_layout->setSpacing(8);

  // 1. Quick action button bar (Undo, Redo, Eraser, Mirror, Reset Rotation, Zoom Fit)
  auto *action_bar = new QHBoxLayout();
  action_bar->setSpacing(4);

  auto make_btn = [this](const QString &icon_path, const QString &tooltip) {
    auto *btn = new QPushButton(this);
    btn->setIcon(IconHelper::load_symbolic(icon_path, palette(), 18));
    btn->setToolTip(tooltip);
    btn->setFixedSize(34, 30);
    return btn;
  };

  auto *undo_btn = make_btn(":/icons/icons/edit-undo-symbolic.svg", tr("Undo"));
  connect(undo_btn, &QPushButton::clicked, this, &QuickMenu::undoRequested);
  action_bar->addWidget(undo_btn);

  auto *redo_btn = make_btn(":/icons/icons/edit-redo-symbolic.svg", tr("Redo"));
  connect(redo_btn, &QPushButton::clicked, this, &QuickMenu::redoRequested);
  action_bar->addWidget(redo_btn);

  eraser_btn_ = make_btn(":/icons/icons/tool-eraser-symbolic.svg", tr("Toggle Eraser"));
  eraser_btn_->setCheckable(true);
  connect(eraser_btn_, &QPushButton::clicked, this, &QuickMenu::toggleEraserRequested);
  action_bar->addWidget(eraser_btn_);

  auto *mirror_btn = make_btn(":/icons/icons/symmetry-h-symbolic.svg", tr("Mirror Canvas"));
  connect(mirror_btn, &QPushButton::clicked, this, &QuickMenu::mirrorCanvasRequested);
  action_bar->addWidget(mirror_btn);

  auto *rot_btn = make_btn(":/icons/icons/pan-down-symbolic.svg", tr("Reset Rotation"));
  connect(rot_btn, &QPushButton::clicked, this, &QuickMenu::resetRotationRequested);
  action_bar->addWidget(rot_btn);

  auto *fit_btn = make_btn(":/icons/icons/pan-end-symbolic.svg", tr("Zoom to Fit"));
  connect(fit_btn, &QPushButton::clicked, this, &QuickMenu::zoomFitRequested);
  action_bar->addWidget(fit_btn);

  root_layout->addLayout(action_bar);

  // 2. Brush Size Scrub Slider
  auto *size_row = new QHBoxLayout();
  size_row->addWidget(new QLabel(tr("Size:"), this));
  size_slider_ = new QSlider(Qt::Horizontal, this);
  size_slider_->setRange(1, 500);
  size_slider_->setValue(static_cast<int>(current_size_));
  size_label_ = new QLabel(QString::number(static_cast<int>(current_size_)) + " px", this);
  size_label_->setFixedWidth(50);
  connect(size_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_controls_) return;
    current_size_ = val;
    size_label_->setText(QString::number(val) + " px");
    emit sizeChanged(val);
  });
  size_row->addWidget(size_slider_);
  size_row->addWidget(size_label_);
  root_layout->addLayout(size_row);

  // 3. Opacity Scrub Slider
  auto *opacity_row = new QHBoxLayout();
  opacity_row->addWidget(new QLabel(tr("Opacity:"), this));
  opacity_slider_ = new QSlider(Qt::Horizontal, this);
  opacity_slider_->setRange(1, 100);
  opacity_slider_->setValue(static_cast<int>(std::round(current_opacity_ * 100.0)));
  opacity_label_ = new QLabel(QString::number(static_cast<int>(std::round(current_opacity_ * 100.0))) + "%", this);
  opacity_label_->setFixedWidth(50);
  connect(opacity_slider_, &QSlider::valueChanged, this, [this](int val) {
    if (updating_controls_) return;
    current_opacity_ = val / 100.0;
    opacity_label_->setText(QString::number(val) + "%");
    emit opacityChanged(current_opacity_);
  });
  opacity_row->addWidget(opacity_slider_);
  opacity_row->addWidget(opacity_label_);
  root_layout->addLayout(opacity_row);

  // 4. Quick Color Swatches (8 slots)
  swatch_layout_ = new QHBoxLayout();
  swatch_layout_->setSpacing(4);
  root_layout->addLayout(swatch_layout_);
  rebuild_swatches();

  // 5. Favorite Brush Presets Grid (2x4 or 4x2)
  preset_grid_ = new QGridLayout();
  preset_grid_->setSpacing(4);
  root_layout->addLayout(preset_grid_);
  rebuild_presets();
}

void QuickMenu::rebuild_swatches() {
  QLayoutItem *item;
  while ((item = swatch_layout_->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }
  swatch_buttons_.clear();

  for (std::size_t i = 0; i < 8; ++i) {
    auto *btn = new QPushButton(this);
    btn->setFixedSize(28, 28);
    if (i < recent_colors_.size()) {
      const auto &c = recent_colors_[i];
      btn->setStyleSheet(QString("background-color: rgb(%1, %2, %3); border: 1px solid rgba(255,255,255,0.25); border-radius: 4px;")
                             .arg(c.r).arg(c.g).arg(c.b));
      connect(btn, &QPushButton::clicked, this, [this, c]() {
        emit colorSelected(c);
      });
    } else {
      btn->setEnabled(false);
      btn->setStyleSheet("background-color: transparent; border: 1px dashed rgba(128,128,128,0.3); border-radius: 4px;");
    }
    swatch_buttons_.push_back(btn);
    swatch_layout_->addWidget(btn);
  }
}

void QuickMenu::rebuild_presets() {
  QLayoutItem *item;
  while ((item = preset_grid_->takeAt(0)) != nullptr) {
    delete item->widget();
    delete item;
  }
  preset_buttons_.clear();

  const int count = std::min(static_cast<int>(presets_.size()), 8);
  for (int i = 0; i < count; ++i) {
    const auto &p = presets_[i];
    auto *btn = new QPushButton(this);
    btn->setFixedSize(62, 54);
    btn->setToolTip(QString::fromStdString(p.display_name));
    btn->setCheckable(true);
    if (active_preset_index_ && *active_preset_index_ == p.db_index) {
      btn->setChecked(true);
    }
    if (!p.preview_pixmap.isNull()) {
      btn->setIcon(QIcon(p.preview_pixmap));
      btn->setIconSize(QSize(48, 36));
    } else if (!p.thumbnail_path.empty()) {
      btn->setIcon(QIcon(QString::fromStdString(p.thumbnail_path.string())));
      btn->setIconSize(QSize(48, 36));
    } else {
      btn->setText(QString::fromStdString(p.display_name).left(6));
    }
    connect(btn, &QPushButton::clicked, this, [this, idx = p.db_index]() {
      emit presetSelected(idx);
      hide();
    });
    preset_buttons_.push_back(btn);
    preset_grid_->addWidget(btn, i / 4, i % 4);
  }
}

void QuickMenu::set_brush_color(core::Rgba8 color) {
  current_color_ = color;
}

void QuickMenu::set_brush_size(double size) {
  current_size_ = size;
  updating_controls_ = true;
  if (size_slider_) size_slider_->setValue(static_cast<int>(size));
  if (size_label_) size_label_->setText(QString::number(static_cast<int>(size)) + " px");
  updating_controls_ = false;
}

void QuickMenu::set_brush_opacity(double opacity) {
  current_opacity_ = opacity;
  updating_controls_ = true;
  const int pct = static_cast<int>(std::round(opacity * 100.0));
  if (opacity_slider_) opacity_slider_->setValue(pct);
  if (opacity_label_) opacity_label_->setText(QString::number(pct) + "%");
  updating_controls_ = false;
}

void QuickMenu::set_is_eraser(bool is_eraser) {
  is_eraser_ = is_eraser;
  if (eraser_btn_) eraser_btn_->setChecked(is_eraser);
}

void QuickMenu::set_recent_colors(const std::vector<core::Rgba8> &colors) {
  recent_colors_ = colors;
  rebuild_swatches();
}

void QuickMenu::set_presets(const std::vector<PresetItem> &presets) {
  presets_ = presets;
  rebuild_presets();
}

void QuickMenu::set_active_preset(std::size_t db_index) {
  active_preset_index_ = db_index;
  for (std::size_t i = 0; i < presets_.size() && i < preset_buttons_.size(); ++i) {
    preset_buttons_[i]->setChecked(presets_[i].db_index == db_index);
  }
}

} // namespace kreart::ui
