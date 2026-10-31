#include "kreart/ui/mobile_editor_view.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>

#include "kreart/io/autosave.h"
#include "kreart/io/kra.h"
#include "kreart/io/png.h"
#include "kreart/io/recovery.h"
#include "kreart/mobile/android_utils.h"
#include "kreart/ui/app_settings.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/mobile/mobile_canvas.h"
#include "kreart/ui/mobile/mobile_hud.h"
#include "kreart/ui/mobile/mobile_sheet.h"
#include "kreart/ui/mobile/mobile_theme.h"
#include "kreart/ui/mobile/pill_button.h"
#include "kreart/ui/mobile/quick_wheel.h"
#include "kreart/ui/mobile/sheets.h"

namespace kreart::ui {
namespace {

using mobile::tokens;

constexpr int kCapsuleMargin = 12;
constexpr int kDockHeight = 64;
constexpr int kSlotSize = 52;
constexpr int kScrubberSize = 44;
const char *const kDockSlotsKey = "mobile/dock_slots";
const char *const kDockSlotsDefault = "0,1,5";

struct ToolInfo {
  Canvas::Tool tool;
  const char *label;
  const char *icon;
};

const ToolInfo kTools[] = {
    {Canvas::Tool::BRUSH, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Brush"),
     ":/icons/icons/tool-brush-symbolic.svg"},
    {Canvas::Tool::ERASER, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Eraser"),
     ":/icons/icons/tool-eraser-symbolic.svg"},
    {Canvas::Tool::SMUDGE, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Smudge"),
     ":/icons/icons/tool-smudge-symbolic.svg"},
    {Canvas::Tool::RECT_SELECT, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Rect Select"),
     ":/icons/icons/tool-rect-select-symbolic.svg"},
    {Canvas::Tool::ELLIPSE_SELECT, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Ellipse Select"),
     ":/icons/icons/tool-ellipse-select-symbolic.svg"},
    {Canvas::Tool::LASSO_SELECT, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Lasso"),
     ":/icons/icons/tool-lasso-symbolic.svg"},
    {Canvas::Tool::MAGIC_WAND, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Magic Wand"),
     ":/icons/icons/tool-wand-symbolic.svg"},
    {Canvas::Tool::MOVE, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Move"),
     ":/icons/icons/tool-move-symbolic.svg"},
    {Canvas::Tool::COLOR_PICKER, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Color Picker"),
     ":/icons/icons/tool-picker-symbolic.svg"},
    {Canvas::Tool::FILL, QT_TRANSLATE_NOOP("kreart::ui::mobile_editor", "Fill"),
     ":/icons/icons/tool-fill-symbolic.svg"},
};

const ToolInfo &tool_info(Canvas::Tool tool) {
  for (const auto &info : kTools) {
    if (info.tool == tool) return info;
  }
  return kTools[0];
}

QString tool_label(Canvas::Tool tool) {
  return QCoreApplication::translate("kreart::ui::mobile_editor", tool_info(tool).label);
}

QString tool_icon(Canvas::Tool tool) {
  return QString::fromUtf8(tool_info(tool).icon);
}

// Rounded translucent chrome panel.
class ChromePanel : public QWidget {
public:
  explicit ChromePanel(QWidget *parent, int radius, int alpha = 240) : QWidget(parent), radius_(radius) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    bg_ = tokens().surface;
    bg_.setAlpha(alpha);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(tokens().hairline, 1.0));
    p.setBrush(bg_);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), radius_, radius_);
  }

private:
  int radius_ = 20;
  QColor bg_;
};

} // namespace

MobileEditorView::MobileEditorView(QWidget *parent)
    : QWidget(parent), document_(1920, 1080) {
  document_.layer(0).set_name("Layer 1");
  build_ui();
  reload_presets();
  update_title_only();
  update_dock_highlights();
  setup_autosave();
  QTimer::singleShot(0, this, [this]() { check_recovery(); });
}

MobileEditorView::~MobileEditorView() = default;

Canvas *MobileEditorView::canvas() {
  return canvas_;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MobileEditorView::build_ui() {
  setObjectName("MobileEditor");
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet(QString("QWidget#MobileEditor { background: %1; }")
                    .arg(mobile::css_color(tokens().workspace_bg)));
  setFocusPolicy(Qt::StrongFocus);

  // Canvas fills the window.
  canvas_ = new MobileCanvas(document_, this);
  canvas_->set_dark_theme(true);
  canvas_->setFocusPolicy(Qt::StrongFocus);
  canvas_->move(0, 0);

  // Context shared with every sheet.
  ctx_.doc = &document_;
  ctx_.canvas = canvas_;
  ctx_.history = &color_history_;
  ctx_.presets = &preset_database_;
  ctx_.previews = &stroke_preview_cache_;
  ctx_.apply_color = [this](core::Rgba8 color, bool add_to_history) {
    apply_brush_color(color, add_to_history);
  };
  ctx_.hud = [this](const QString &text) { hud_->show_message(text); };
  ctx_.save_document = [this]() { return save_to(current_path_); };
  ctx_.save_document_as = [this](const std::filesystem::path &path) {
    if (!save_to(path)) return false;
    current_path_ = path;
    project_title_ = QString::fromStdString(path.stem().string());
    update_title_only();
    return true;
  };
  ctx_.export_png = [this](const std::filesystem::path &path) { return export_png(path); };
  ctx_.import_image_as_layer = [this](const std::filesystem::path &path) {
    return import_image_as_layer(path);
  };
  ctx_.close_to_gallery = [this]() { request_back_to_gallery(); };
  ctx_.reload_presets = [this]() { reload_presets(); };
  ctx_.set_tool = [this](Canvas::Tool tool) { set_active_tool(tool); };
  ctx_.document_structure_changed = [this]() {
    update_title_only();
    canvas_->refresh();
  };
  ctx_.open_sheet = [this](const QString &id) { show_sheet(id); };

  // Owned by unique_ptr only: parenting it to `this` as well would double-delete.
  sheets_ = std::make_unique<mobile::EditorSheets>(this, ctx_, nullptr);

  hud_ = new mobile::MobileHud(this);

  dock_long_press_timer_ = new QTimer(this);
  dock_long_press_timer_->setSingleShot(true);
  connect(dock_long_press_timer_, &QTimer::timeout, this, [this]() {
    const int slot = dock_press_slot_;
    dock_press_slot_ = -1;
    if (slot >= 0) open_tool_chooser(slot);
  });

  // Quick wheel replaces the desktop QuickMenu on touch.
  quick_wheel_ = new mobile::QuickWheel(this);
  connect(quick_wheel_, &mobile::QuickWheel::undoRequested, this, [this]() {
    if (document_.undo()) {
      canvas_->refresh();
      update_title_only();
    }
  });
  connect(quick_wheel_, &mobile::QuickWheel::redoRequested, this, [this]() {
    if (document_.redo()) {
      canvas_->refresh();
      update_title_only();
    }
  });
  connect(quick_wheel_, &mobile::QuickWheel::toggleEraserRequested, this, [this]() {
    set_active_tool(canvas_->tool() == Canvas::Tool::ERASER ? Canvas::Tool::BRUSH
                                                           : Canvas::Tool::ERASER);
  });
  connect(quick_wheel_, &mobile::QuickWheel::zoomFitRequested, canvas_, &Canvas::zoom_fit);
  connect(quick_wheel_, &mobile::QuickWheel::mirrorRequested, canvas_,
          &Canvas::toggle_mirror_horizontal);
  connect(quick_wheel_, &mobile::QuickWheel::resetRotationRequested, canvas_, &Canvas::reset_rotation);
  connect(quick_wheel_, &mobile::QuickWheel::brushSizeIncreaseRequested, this, [this]() {
    canvas_->set_brush_size(std::min(500.0, canvas_->brush_size() * 1.25));
    hud_->show_message(QString("%1 px").arg(static_cast<int>(std::lround(canvas_->brush_size()))));
  });
  connect(quick_wheel_, &mobile::QuickWheel::brushSizeDecreaseRequested, this, [this]() {
    canvas_->set_brush_size(std::max(1.0, canvas_->brush_size() / 1.25));
    hud_->show_message(QString("%1 px").arg(static_cast<int>(std::lround(canvas_->brush_size()))));
  });
  connect(quick_wheel_, &mobile::QuickWheel::colorRequested, this,
          [this]() { show_sheet(QStringLiteral("color")); });

  // Canvas signals.
  connect(canvas_, &Canvas::documentChanged, this, [this]() { update_title_only(); });
  connect(canvas_, &Canvas::colorPicked, this, [this](const core::Rgba8 &color) {
    apply_brush_color(color, true);
    hud_->show_message(tr("Color picked"));
  });
  connect(canvas_, &Canvas::quickMenuRequested, this, [this](double x, double y) {
    quick_wheel_->set_center_color(QColor(canvas_->brush_settings().color.r,
                                          canvas_->brush_settings().color.g,
                                          canvas_->brush_settings().color.b));
    quick_wheel_->set_eraser_active(canvas_->tool() == Canvas::Tool::ERASER);
    quick_wheel_->open_at(QPointF(x, y));
  });

  auto *touch_canvas = static_cast<MobileCanvas *>(canvas_);
  connect(touch_canvas, &MobileCanvas::longPressAt, this, [this](QPointF pos) {
    quick_wheel_->set_center_color(QColor(canvas_->brush_settings().color.r,
                                          canvas_->brush_settings().color.g,
                                          canvas_->brush_settings().color.b));
    quick_wheel_->set_eraser_active(canvas_->tool() == Canvas::Tool::ERASER);
    quick_wheel_->open_at(pos);
  });
  connect(touch_canvas, &MobileCanvas::threeFingerTap, this, [this]() {
    if (document_.undo()) {
      canvas_->refresh();
      update_title_only();
      hud_->show_message(tr("Undo"));
    }
  });
  connect(touch_canvas, &MobileCanvas::threeFingerDoubleTap, this, [this]() {
    if (document_.redo()) {
      canvas_->refresh();
      update_title_only();
      hud_->show_message(tr("Redo"));
    }
  });
  connect(touch_canvas, &MobileCanvas::twoFingerTap, this,
          [this]() { open_tool_chooser(0); });
  connect(touch_canvas, &MobileCanvas::twoFingerDoubleTap, this, [this]() {
    quick_wheel_->open_at(QPointF(width() * 0.5, height() * 0.5));
  });

  setup_top_capsule();
  setup_bottom_dock();
  setup_scrubber();
  refresh_theme_icons();
  layout_overlays();
}

void MobileEditorView::setup_top_capsule() {
  auto *capsule = new ChromePanel(this, 20, 240);
  capsule_ = capsule;

  auto *layout = new QHBoxLayout(capsule);
  layout->setContentsMargins(6, 6, 8, 6);
  layout->setSpacing(6);

  back_btn_ = new QPushButton(capsule);
  back_btn_->setFixedSize(44, 44);
  back_btn_->setFlat(true);
  back_btn_->setCursor(Qt::PointingHandCursor);
  back_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/go-down-symbolic.svg",
                                               tokens().text, 20));
  back_btn_->setStyleSheet("background: transparent; border: none;");
  back_btn_->setToolTip(tr("Back to gallery"));
  connect(back_btn_, &QPushButton::clicked, this, [this]() {
    if (close_topmost_overlay()) return;
    request_back_to_gallery();
  });
  layout->addWidget(back_btn_);

  auto *title_col = new QVBoxLayout();
  title_col->setContentsMargins(0, 0, 0, 0);
  title_col->setSpacing(0);
  title_label_ = new QLabel(capsule);
  title_label_->setFont(mobile::font_body());
  title_label_->setMinimumWidth(0);
  title_label_->setMaximumWidth(160);
  title_col->addWidget(title_label_);
  subtitle_label_ = new QLabel(capsule);
  subtitle_label_->setFont(mobile::font_caption());
  subtitle_label_->setStyleSheet(QString("color: %1;").arg(mobile::css_color(tokens().text_muted)));
  title_col->addWidget(subtitle_label_);
  layout->addLayout(title_col, 1);

  undo_btn_ = new QPushButton(capsule);
  undo_btn_->setFixedSize(44, 44);
  undo_btn_->setFlat(true);
  undo_btn_->setCursor(Qt::PointingHandCursor);
  undo_btn_->setStyleSheet("background: transparent; border: none;");
  connect(undo_btn_, &QPushButton::clicked, this, [this]() {
    if (document_.undo()) {
      canvas_->refresh();
      update_title_only();
    }
  });
  layout->addWidget(undo_btn_);

  redo_btn_ = new QPushButton(capsule);
  redo_btn_->setFixedSize(44, 44);
  redo_btn_->setFlat(true);
  redo_btn_->setCursor(Qt::PointingHandCursor);
  redo_btn_->setStyleSheet("background: transparent; border: none;");
  connect(redo_btn_, &QPushButton::clicked, this, [this]() {
    if (document_.redo()) {
      canvas_->refresh();
      update_title_only();
    }
  });
  layout->addWidget(redo_btn_);

  overflow_btn_ = new QPushButton(capsule);
  overflow_btn_->setFixedSize(44, 44);
  overflow_btn_->setFlat(true);
  overflow_btn_->setCursor(Qt::PointingHandCursor);
  overflow_btn_->setText(QString::fromUtf8("\u22ef"));
  overflow_btn_->setFont(mobile::font_title());
  overflow_btn_->setStyleSheet(
      QString("background: transparent; border: none; color: %1;")
          .arg(mobile::css_color(tokens().text)));
  connect(overflow_btn_, &QPushButton::clicked, this, [this]() {
    show_sheet(QStringLiteral("hub"));
  });
  layout->addWidget(overflow_btn_);

  capsule->show();

  // Single peek pill shown while the chrome is hidden in fullscreen canvas mode.
  auto *peek = new QPushButton(this);
  peek->setFixedSize(44, 22);
  peek->setCursor(Qt::PointingHandCursor);
  peek->setStyleSheet(
      QString("QPushButton { background: rgba(22,24,29,0.94); border: 1px solid %1;"
              " border-radius: 11px; }")
          .arg(mobile::css_color(tokens().hairline, true)));
  connect(peek, &QPushButton::clicked, this, [this]() {
    if (fullscreen_canvas_) toggle_fullscreen_chrome();
  });
  peek->hide();
  peek_pill_ = peek;
}

void MobileEditorView::setup_bottom_dock() {
  auto *dock = new ChromePanel(this, 22, 244);
  dock_ = dock;

  auto *layout = new QHBoxLayout(dock);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  for (int i = 0; i < 3; ++i) {
    auto *btn = new QPushButton(dock);
    btn->setFixedSize(kSlotSize, kSlotSize);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(24, 24));
    btn->setStyleSheet(
        "QPushButton { background: #1E2127; border: none; border-radius: 16px; }"
        "QPushButton:pressed { background: #2A2E36; }");
    btn->setAttribute(Qt::WA_AcceptTouchEvents, true);
    const int slot = i;
    connect(btn, &QPushButton::clicked, this, [this, slot]() {
      set_active_tool(dock_slots_[static_cast<std::size_t>(slot)]);
    });
    // Long press opens the tool chooser for this slot.
    btn->installEventFilter(this);
    btn->setProperty("slot_index", slot);
    layout->addWidget(btn);
    slot_buttons_[static_cast<std::size_t>(i)] = btn;
  }

  color_chip_ = new QPushButton(dock);
  color_chip_->setFixedSize(kSlotSize, kSlotSize);
  color_chip_->setCursor(Qt::PointingHandCursor);
  connect(color_chip_, &QPushButton::clicked, this, [this]() {
    show_sheet(QStringLiteral("color"));
  });
  layout->addWidget(color_chip_);

  studio_btn_ = new QPushButton(dock);
  studio_btn_->setFixedSize(kSlotSize, kSlotSize);
  studio_btn_->setCursor(Qt::PointingHandCursor);
  studio_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/emblem-system-symbolic.svg",
                                                 tokens().text, 22));
  studio_btn_->setIconSize(QSize(22, 22));
  studio_btn_->setStyleSheet(
      "QPushButton { background: #1E2127; border: none; border-radius: 16px; }"
      "QPushButton:pressed { background: #2A2E36; }");
  studio_btn_->setToolTip(tr("Studio"));
  connect(studio_btn_, &QPushButton::clicked, this, [this]() {
    show_sheet(QStringLiteral("hub"));
  });
  layout->addWidget(studio_btn_);

  dock->show();
  dock_slots_ = load_dock_slots();
}

void MobileEditorView::setup_scrubber() {
  auto *scrubber = new QWidget(this);
  scrubber_ = scrubber;
  scrubber->setFixedSize(kScrubberSize, kScrubberSize);
  scrubber->setCursor(Qt::SizeHorCursor);
  scrubber->setAttribute(Qt::WA_NoSystemBackground, true);
  scrubber->installEventFilter(this);

  scrubber_label_ = new QLabel(scrubber);
  scrubber_label_->setAlignment(Qt::AlignCenter);
  scrubber_label_->setFont(mobile::font_caption());
  scrubber_label_->setStyleSheet(
      QString("color: %1; background: transparent;").arg(mobile::css_color(tokens().text)));
  scrubber_label_->setGeometry(0, 0, kScrubberSize, kScrubberSize);
  scrubber_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  scrubber_label_->setText(QString::number(static_cast<int>(std::lround(canvas_->brush_size()))));
  scrubber->show();
}

void MobileEditorView::refresh_theme_icons() {
  undo_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/edit-undo-symbolic.svg",
                                               tokens().text, 20));
  redo_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/edit-redo-symbolic.svg",
                                               tokens().text, 20));
  update_dock_highlights();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void MobileEditorView::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  layout_overlays();
  if (sheets_) sheets_->relayout();
}

void MobileEditorView::layout_overlays() {
  if (!canvas_) return;
  canvas_->setGeometry(0, 0, width(), height());

  const int safe_top = kCapsuleMargin;
  const int safe_bottom = kCapsuleMargin;

  if (capsule_) {
    const int capsule_h = 56;
    capsule_->setGeometry(kCapsuleMargin, safe_top,
                          std::max(0, width() - 2 * kCapsuleMargin), capsule_h);
  }
  if (peek_pill_) {
    peek_pill_->setGeometry((width() - 44) / 2, safe_top, 44, 22);
  }

  if (dock_) {
    const int dock_w = 5 * kSlotSize + 6 * 6 + 12;
    dock_->setGeometry((width() - dock_w) / 2, height() - safe_bottom - kDockHeight,
                       dock_w, kDockHeight);
  }
  if (scrubber_) {
    scrubber_->setGeometry(12, height() - safe_bottom - kDockHeight - kScrubberSize - 8,
                           kScrubberSize, kScrubberSize);
  }
  if (hud_) {
    hud_->set_anchor(width() / 2, height() - safe_bottom - kDockHeight - 16);
  }
  if (tool_tray_) {
    const int tray_w = std::min(width() - 24, 5 * 60 + 24);
    const int tray_h = 2 * 60 + 16;
    tool_tray_->setGeometry((width() - tray_w) / 2,
                            height() - safe_bottom - kDockHeight - tray_h - 12, tray_w, tray_h);
  }
}

void MobileEditorView::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  layout_overlays();
}

// ---------------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------------

void MobileEditorView::update_title_only() {
  if (!title_label_) return;
  const QString mod = document_.modified() ? QStringLiteral(" \u2022") : QString();
  title_label_->setText(project_title_ + mod);
  title_label_->setToolTip(project_title_);
  const QFontMetrics fm(title_label_->font());
  title_label_->setText(fm.elidedText(project_title_ + mod, Qt::ElideRight, 160));

  if (subtitle_label_) {
    subtitle_label_->setText(QString("%1\u00d7%2 \u2022 %3%")
                                 .arg(document_.width())
                                 .arg(document_.height())
                                 .arg(static_cast<int>(std::lround(canvas_->zoom_percent()))));
  }
  if (undo_btn_) undo_btn_->setEnabled(document_.can_undo());
  if (redo_btn_) redo_btn_->setEnabled(document_.can_redo());
}

// ---------------------------------------------------------------------------
// Tools and dock
// ---------------------------------------------------------------------------

std::array<Canvas::Tool, 3> MobileEditorView::load_dock_slots() const {
  const std::array<Canvas::Tool, 3> fallback{Canvas::Tool::BRUSH, Canvas::Tool::ERASER,
                                             Canvas::Tool::LASSO_SELECT};
  QSettings settings(QStringLiteral("io.github.kreart"), QStringLiteral("Kreart"));
  const QString raw = settings.value(QString::fromLatin1(kDockSlotsKey),
                                     QString::fromLatin1(kDockSlotsDefault))
                          .toString();
  const QStringList parts = raw.split(',');
  if (parts.size() != 3) return fallback;

  std::array<Canvas::Tool, 3> loaded = fallback;
  for (int i = 0; i < 3; ++i) {
    bool ok = false;
    const int value = parts[i].trimmed().toInt(&ok);
    if (!ok || value < 0 || value > static_cast<int>(Canvas::Tool::FILL)) {
      return fallback;
    }
    loaded[static_cast<std::size_t>(i)] = static_cast<Canvas::Tool>(value);
  }
  return loaded;
}

void MobileEditorView::save_dock_slots() {
  QSettings settings(QStringLiteral("io.github.kreart"), QStringLiteral("Kreart"));
  const QString value = QString("%1,%2,%3")
                            .arg(static_cast<int>(dock_slots_[0]))
                            .arg(static_cast<int>(dock_slots_[1]))
                            .arg(static_cast<int>(dock_slots_[2]));
  settings.setValue(QString::fromLatin1(kDockSlotsKey), value);
}

void MobileEditorView::update_dock_highlights() {
  const Canvas::Tool active = canvas_ ? canvas_->tool() : Canvas::Tool::BRUSH;
  for (int i = 0; i < 3; ++i) {
    auto *btn = slot_buttons_[static_cast<std::size_t>(i)];
    if (!btn) continue;
    const Canvas::Tool tool = dock_slots_[static_cast<std::size_t>(i)];
    const bool is_active = (tool == active);
    btn->setIcon(IconHelper::load_symbolic(tool_icon(tool),
                                           is_active ? tokens().accent_ink : tokens().text, 24));
    btn->setStyleSheet(
        QString("QPushButton { background: %1; border: none; border-radius: 16px; }"
                "QPushButton:pressed { background: #2A2E36; }")
            .arg(is_active ? mobile::css_color(tokens().accent)
                           : QStringLiteral("#1E2127")));
    btn->setToolTip(tool_label(tool));
  }

  if (color_chip_) {
    const core::Rgba8 c = canvas_->brush_settings().color;
    color_chip_->setStyleSheet(
        QString("QPushButton { background: rgba(%1,%2,%3,%4); border: 2px solid rgba(255,255,255,0.28);"
                " border-radius: 26px; }")
            .arg(c.r).arg(c.g).arg(c.b).arg(c.a));
  }
  if (scrubber_label_ && canvas_) {
    scrubber_label_->setText(QString::number(static_cast<int>(std::lround(canvas_->brush_size()))));
  }
}

void MobileEditorView::set_active_tool(Canvas::Tool tool) {
  if (!canvas_) return;
  canvas_->set_tool(tool);
  update_dock_highlights();
}

void MobileEditorView::assign_dock_slot(int slot, Canvas::Tool tool) {
  if (slot < 0 || slot > 2) return;
  dock_slots_[static_cast<std::size_t>(slot)] = tool;
  save_dock_slots();
  set_active_tool(tool);
}

void MobileEditorView::open_tool_chooser(int target_slot) {
  tray_target_slot_ = std::clamp(target_slot, 0, 2);
  if (tool_tray_) {
    tool_tray_->deleteLater();
    tool_tray_ = nullptr;
  }

  auto *tray = new ChromePanel(this, 20, 250);
  tool_tray_ = tray;
  auto *layout = new QGridLayout(tray);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setHorizontalSpacing(6);
  layout->setVerticalSpacing(6);

  int index = 0;
  for (const auto &info : kTools) {
    auto *btn = new QPushButton(tray);
    btn->setFixedSize(52, 52);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIcon(IconHelper::load_symbolic(QString::fromUtf8(info.icon), tokens().text, 24));
    btn->setIconSize(QSize(24, 24));
    btn->setToolTip(QCoreApplication::translate("kreart::ui::mobile_editor", info.label));
    btn->setStyleSheet(
        "QPushButton { background: #1E2127; border: none; border-radius: 14px; }"
        "QPushButton:pressed { background: #2A2E36; }");
    const Canvas::Tool tool = info.tool;
    connect(btn, &QPushButton::clicked, this, [this, tool]() {
      assign_dock_slot(tray_target_slot_, tool);
      if (tool_tray_) {
        tool_tray_->hide();
        tool_tray_->deleteLater();
        tool_tray_ = nullptr;
      }
    });
    layout->addWidget(btn, index / 5, index % 5);
    ++index;
  }

  tray->show();
  tray->raise();
  layout_overlays();
}

// ---------------------------------------------------------------------------
// Event filter: dock long-press and scrubber drag
// ---------------------------------------------------------------------------

bool MobileEditorView::eventFilter(QObject *obj, QEvent *event) {
  // Dock slot long-press -> tool chooser for that slot.
  for (int i = 0; i < 3; ++i) {
    if (obj != slot_buttons_[static_cast<std::size_t>(i)]) continue;
    if (event->type() == QEvent::MouseButtonPress) {
      auto *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton) {
        dock_press_slot_ = i;
        dock_long_press_timer_->start(500);
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      dock_long_press_timer_->stop();
      dock_press_slot_ = -1;
    } else if (event->type() == QEvent::MouseMove && dock_press_slot_ >= 0) {
      // Any meaningful movement means a drag, not a long press.
      dock_long_press_timer_->stop();
      dock_press_slot_ = -1;
    }
    break;
  }

  if (obj == scrubber_) {
    if (event->type() == QEvent::MouseButtonPress) {
      auto *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::LeftButton) {
        scrubbing_ = true;
        scrub_start_x_ = static_cast<int>(me->globalPosition().x());
        scrub_start_size_ = canvas_->brush_size();
        hud_->show_message(QString("%1 px").arg(static_cast<int>(std::lround(scrub_start_size_))), 0);
        return true;
      }
    } else if (event->type() == QEvent::MouseMove && scrubbing_) {
      auto *me = static_cast<QMouseEvent *>(event);
      const int dx = static_cast<int>(me->globalPosition().x()) - scrub_start_x_;
      // 4 px of travel per size step keeps fine control at the low end.
      const double new_size = std::clamp(scrub_start_size_ + dx / 4.0, 1.0, 500.0);
      canvas_->set_brush_size(new_size);
      scrubber_label_->setText(QString::number(static_cast<int>(std::lround(new_size))));
      hud_->show_message(QString("%1 px").arg(static_cast<int>(std::lround(new_size))), 0);
      return true;
    } else if (event->type() == QEvent::MouseButtonRelease && scrubbing_) {
      scrubbing_ = false;
      hud_->show_message(QString("%1 px").arg(static_cast<int>(std::lround(canvas_->brush_size()))));
      return true;
    }
  }

  return QWidget::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// Overlays / back handling
// ---------------------------------------------------------------------------

void MobileEditorView::toggle_fullscreen_chrome() {
  fullscreen_canvas_ = !fullscreen_canvas_;
  if (capsule_) capsule_->setVisible(!fullscreen_canvas_);
  if (peek_pill_) peek_pill_->setVisible(fullscreen_canvas_);
  if (dock_) dock_->setVisible(!fullscreen_canvas_);
  if (scrubber_) scrubber_->setVisible(!fullscreen_canvas_);
}

bool MobileEditorView::close_topmost_overlay() {
  if (tool_tray_) {
    tool_tray_->hide();
    tool_tray_->deleteLater();
    tool_tray_ = nullptr;
    return true;
  }
  if (sheets_ && sheets_->close_active()) return true;
  if (quick_wheel_ && quick_wheel_->is_open()) {
    quick_wheel_->close_wheel();
    return true;
  }
  if (fullscreen_canvas_) {
    toggle_fullscreen_chrome();
    return true;
  }
  return false;
}

void MobileEditorView::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
    if (close_topmost_overlay()) {
      event->accept();
      return;
    }
    request_back_to_gallery();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void MobileEditorView::request_back_to_gallery() {
  if (saving_) return;
  save_current_document();
  emit backToGalleryRequested();
}

void MobileEditorView::show_sheet(const QString &id) {
  if (!sheets_) return;
  if (quick_wheel_) quick_wheel_->close_wheel();
  sheets_->show(id);
}

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

void MobileEditorView::apply_brush_color(core::Rgba8 color, bool add_to_history) {
  if (!canvas_) return;
  canvas_->set_brush_color(color);
  if (add_to_history) color_history_.record(color);
  update_dock_highlights();
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

void MobileEditorView::reload_presets() {
  preset_database_.clear();
  preset_database_.clear_search_paths();
  for (const auto &path : resources::ResourceDatabase::standard_preset_paths(false)) {
    preset_database_.add_search_path(path);
  }
  preset_database_.add_search_path(
      resources::ResourceDatabase::user_resource_directory() / "presets");
  preset_database_.scan(true, false);
}

// ---------------------------------------------------------------------------
// Documents
// ---------------------------------------------------------------------------

void MobileEditorView::assign_layer_ids() {
  std::size_t next = 1;
  for (std::size_t i = 0; i < document_.layer_count(); ++i) {
    auto &layer = document_.layer(i);
    if (layer.id() == 0) {
      layer.set_id(next);
    }
    next = std::max(next, layer.id() + 1);
  }
}

void MobileEditorView::mark_clean() {
  document_.mark_saved();
  update_title_only();
}

void MobileEditorView::load_document(const std::filesystem::path &path) {
  if (path.empty()) return;

  const QString suffix = QString::fromStdString(path.extension().string()).toLower();
  std::string err;
  bool loaded = false;

  if (suffix == ".kra") {
    loaded = io::read_kra(path, document_, err);
    if (!loaded) {
      // Check if damaged project can be reconstructed from WAL
      std::filesystem::path wal_path;
      if (io::has_uncommitted_wal(path, wal_path)) {
        std::string rec_report;
        if (io::recover_document_from_wal(wal_path, document_, rec_report)) {
          loaded = true;
          err.clear();
          if (hud_) hud_->show_message(tr("Recovered damaged project from journal"));
        }
      }
    } else {
      // If loaded, check for uncommitted strokes in companion WAL
      std::filesystem::path wal_path;
      if (io::has_uncommitted_wal(path, wal_path)) {
        std::string rec_report;
        if (io::recover_document_from_wal(wal_path, document_, rec_report)) {
          if (hud_) hud_->show_message(tr("Replayed uncommitted strokes"));
        }
      }
    }
  }
  if (!loaded) {
    // PNG (or any QImage-readable file): one-layer document named after the stem.
    QImage img(QString::fromStdString(path.string()));
    if (!img.isNull()) {
      img = img.convertToFormat(QImage::Format_ARGB32);
      document_.reset(img.width(), img.height());
      document_.layer(0).set_name(path.stem().string().empty() ? "Layer 1"
                                                               : path.stem().string());
      auto &layer_img = document_.layer(0).image();
      for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
          const QRgb px = line[x];
          const std::uint8_t a = qAlpha(px);
          if (a > 0) {
            layer_img.set_pixel(x, y,
                                core::Rgba8{static_cast<std::uint8_t>(qRed(px)),
                                            static_cast<std::uint8_t>(qGreen(px)),
                                            static_cast<std::uint8_t>(qBlue(px)), a});
          }
        }
      }
      document_.mark_composite_dirty();
      loaded = true;
      err.clear();
    }
  }

  if (!loaded) {
    if (hud_) hud_->show_message(tr("Could not open the file"));
    return;
  }

  current_path_ = path;
  project_title_ = QString::fromStdString(path.stem().string());
  // KRA reading assigns no layer IDs; give every zero-ID layer a unique one so
  // GL layer-texture caching and undo restore stay correct.
  assign_layer_ids();
  document_.mark_composite_dirty();
  canvas_->refresh();
  canvas_->zoom_fit();
  // io::read_kra leaves modified()==true.
  document_.mark_saved();
  update_title_only();
  update_dock_highlights();
}

void MobileEditorView::create_new_document(int width, int height, const QString &title) {
  document_.reset(std::max(16, width), std::max(16, height));
  if (document_.layer_count() > 0) {
    document_.layer(0).set_name("Layer 1");
  }
  project_title_ = title.trimmed().isEmpty() ? QStringLiteral("Untitled") : title.trimmed();

  const std::filesystem::path dir = kreart::mobile::mobile_projects_dir();
  current_path_ = kreart::mobile::unique_path_in(dir, project_title_.toStdString(), ".kra");

  canvas_->refresh();
  canvas_->zoom_fit();
  save_current_document();
  update_title_only();
  update_dock_highlights();
}

bool MobileEditorView::save_to(const std::filesystem::path &path) {
  if (path.empty()) {
    if (hud_) hud_->show_message(tr("No file name"));
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  document_.mark_composite_dirty();
  const auto composite = document_.composite_with_floating();

  std::string err;
  if (!io::write_kra(path, document_, err)) {
    if (hud_) hud_->show_message(tr("Save failed: %1").arg(QString::fromStdString(err)));
    return false;
  }

  // Sidecar thumbnail for the gallery.
  std::filesystem::path thumb = path.parent_path() / (path.stem().string() + ".thumb.png");
  std::string thumb_err;
  io::write_png(thumb, composite, thumb_err);

  io::remove_autosave(path);
  document_.mark_saved();
  update_title_only();
  if (hud_) hud_->show_message(tr("Saved"));
  return true;
}

void MobileEditorView::save_current_document() {
  if (saving_) return;
  saving_ = true;
  if (current_path_.empty()) {
    current_path_ = kreart::mobile::unique_path_in(kreart::mobile::mobile_projects_dir(),
                                           project_title_.toStdString(), ".kra");
  }
  save_to(current_path_);
  saving_ = false;
}

bool MobileEditorView::export_png(const std::filesystem::path &path) {
  document_.mark_composite_dirty();
  const auto composite = document_.composite_with_floating();
  std::string err;
  if (!io::write_png(path, composite, err)) {
    if (hud_) hud_->show_message(tr("Export failed: %1").arg(QString::fromStdString(err)));
    return false;
  }
  if (hud_) hud_->show_message(tr("Exported %1").arg(QString::fromStdString(path.filename().string())));
  return true;
}

bool MobileEditorView::import_image_as_layer(const std::filesystem::path &path) {
  QImage img(QString::fromStdString(path.string()));
  if (img.isNull()) {
    if (hud_) hud_->show_message(tr("Could not read the image"));
    return false;
  }
  img = img.convertToFormat(QImage::Format_ARGB32);

  document_.begin_change();
  std::string layer_name = path.stem().string();
  if (layer_name.empty()) layer_name = "Imported Image";
  const std::size_t new_index = document_.add_layer(layer_name, false);

  const int offset_x = (document_.width() - img.width()) / 2;
  const int offset_y = (document_.height() - img.height()) / 2;
  auto &layer_img = document_.layer(new_index).image();
  for (int y = 0; y < img.height(); ++y) {
    const int doc_y = offset_y + y;
    if (doc_y < 0 || doc_y >= document_.height()) continue;
    const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      const int doc_x = offset_x + x;
      if (doc_x < 0 || doc_x >= document_.width()) continue;
      const QRgb px = line[x];
      const std::uint8_t a = qAlpha(px);
      if (a > 0) {
        layer_img.set_pixel(doc_x, doc_y,
                            core::Rgba8{static_cast<std::uint8_t>(qRed(px)),
                                        static_cast<std::uint8_t>(qGreen(px)),
                                        static_cast<std::uint8_t>(qBlue(px)), a});
      }
    }
  }
  document_.set_active_layer_index(new_index);
  document_.commit_change();
  document_.mark_composite_dirty();
  canvas_->refresh();
  if (hud_) hud_->show_message(tr("Image imported"));
  return true;
}

// ---------------------------------------------------------------------------
// Autosave
// ---------------------------------------------------------------------------

void MobileEditorView::setup_autosave() {
  autosave_timer_ = new QTimer(this);
  autosave_timer_->setInterval(60 * 1000);
  connect(autosave_timer_, &QTimer::timeout, this, [this]() {
    if (!document_.modified() || current_path_.empty()) return;
    std::filesystem::path saved;
    std::string err;
    io::perform_autosave(document_, current_path_, saved, err);
  });
  autosave_timer_->start();

  // Also autosave when the app goes to the background.
  if (window() && window()->windowHandle()) {
    connect(window()->windowHandle(), &QWindow::visibilityChanged, this, [this](QWindow::Visibility) {
      if (!document_.modified() || current_path_.empty()) return;
      std::filesystem::path saved;
      std::string err;
      io::perform_autosave(document_, current_path_, saved, err);
    });
  }
}

void MobileEditorView::check_recovery() {
  if (current_path_.empty() || !hud_) return;
  std::filesystem::path auto_path;
  if (!io::has_autosave(current_path_, auto_path)) return;

  const std::filesystem::path original = current_path_;
  auto *sheet = new mobile::MobileSheet(this, tr("Unsaved recovery"), false);
  auto *body = new QWidget(sheet);
  auto *layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *label = new QLabel(
      tr("An autosaved version of this document is newer than the file on disk. Recover it?"),
      body);
  label->setWordWrap(true);
  layout->addWidget(label);
  sheet->set_body(body);

  sheet->set_secondary_action(tr("Discard"), [this, original, sheet]() {
    io::remove_autosave(original);
    sheet->close();
  });
  sheet->set_primary_action(tr("Recover"), [this, original, auto_path, sheet]() {
    sheet->close();
    load_document(auto_path);
    current_path_ = original;
    project_title_ = QString::fromStdString(original.stem().string());
    document_.mark_composite_dirty();
    canvas_->refresh();
    update_title_only();
  });

  connect(sheet, &mobile::MobileSheet::closed, sheet, &QObject::deleteLater);
  sheet->open();
}

} // namespace kreart::ui
