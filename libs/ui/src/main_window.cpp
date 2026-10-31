#include "kreart/ui/main_window.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/io/kra.h"
#include "kreart/io/png.h"
#include "kreart/io/autosave.h"
#include "kreart/io/recovery.h"
#include "kreart/io/wal_journal.h"
#include "kreart/ui/timelapse_dialog.h"
#include "kreart/ui/project_gallery_window.h"
#include "kreart/ui/grid_dialog.h"
#include "kreart/ui/gallery_view.h"

#include <numbers>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QPainter>
#include <QScrollArea>
#include <QSplitter>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenu>
#include <QToolButton>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include <QFileInfo>
#include <QStyleHints>
#include <QGuiApplication>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include "kreart/ui/desktop_theme.h"
#include "kreart/ui/shortcuts.h"
#include "kreart/ui/shortcut_dialog.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace kreart::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), document_(1920, 1080) {
  setWindowTitle("Kreart");
  resize(1400, 900);
  setAcceptDrops(true);
  if (document_.layer_count() > 0 && document_.layer(0).name() == "1") {
    document_.layer(0).set_name("Layer 1");
  }

  canvas_ = new Canvas(document_, this);
  canvas_->set_stroke_history_enabled(true);
  setCentralWidget(canvas_);
  canvas_->setFocusPolicy(Qt::StrongFocus);
  qApp->installEventFilter(this);

  setup_actions();
  setup_menus();
  setup_tool_rail();
  setup_studio_dock();
  setup_status_bar();
  setup_quick_menu();
  setup_autosave();

  // saveState()/restoreState() only persist objects that carry an objectName,
  // which is why the dock and tool rail are named above.
  {
    QSettings ws("io.github.kreart", "Kreart");
    const QByteArray geo = ws.value("window/geometry").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    const QByteArray st = ws.value("window/state").toByteArray();
    if (!st.isEmpty()) restoreState(st);
    if (studio_tabs_) studio_tabs_->setCurrentIndex(ws.value("window/studio_tab", 0).toInt());
  }

  if (auto *clip = QGuiApplication::clipboard()) {
    connect(clip, &QClipboard::dataChanged, this, [this]() {
      system_clipboard_newer_ = true;
    });
  }
  load_presets();
  rebuild_layer_list();
  refresh_theme_icons();
  update_title();
  update_actions();

  color_history_.record(canvas_->brush_settings().color);
  apply_brush_color(canvas_->brush_settings().color, false);
  // Connect canvas signals
  connect(canvas_, &Canvas::cursorMoved, this, [this](const Canvas::PointerInfo &info) {
    if (cursor_label_) {
      cursor_label_->setText(QString("X: %1  Y: %2").arg(static_cast<int>(info.x)).arg(static_cast<int>(info.y)));
    }
    if (pressure_label_) {
      pressure_label_->setText(info.has_pressure ? QString("Pressure: %1%").arg(static_cast<int>(std::round(info.pressure * 100.0))) : "Pressure: --");
    }
    if (tilt_label_) {
      const double deg = std::hypot(info.tilt_x, info.tilt_y) * 180.0 / std::numbers::pi;
      tilt_label_->setText(info.has_tilt ? QString("Tilt: %1°").arg(static_cast<int>(std::round(deg))) : "Tilt: --");
    }
  });

  connect(canvas_, &Canvas::zoomChanged, this, [this](double percent) {
    if (zoom_label_) zoom_label_->setText(QString("%1%").arg(static_cast<int>(std::round(percent))));
  });

  connect(canvas_, &Canvas::documentChanged, this, [this]() {
    update_title();
    update_actions();
  });

  connect(canvas_, &Canvas::colorPicked, this, [this](const core::Rgba8 &color) {
    apply_brush_color(color, true);
  });

  connect(canvas_, &Canvas::quickMenuRequested, this, [this](double x, double y) {
    popup_quick_menu(x, y);
  });

  apply_pressure_curve_from_settings();
  connect(&AppSettings::instance(), &AppSettings::pressureCurveChanged, this,
          [this](const QString &) { apply_pressure_curve_from_settings(); });

  rebuild_recent_menu();
  connect(&AppSettings::instance(), &AppSettings::recentFilesChanged, this,
          &MainWindow::rebuild_recent_menu);
  connect(&AppSettings::instance(), &AppSettings::detectKritaBrushesChanged, this,
          &MainWindow::load_presets);

  connect(&AppSettings::instance(), &AppSettings::themeChanged, this, [this](ThemeMode) {
    apply_desktop_theme();
    refresh_theme_icons();
    stroke_preview_cache_.clear();
    rebuild_preset_dock();
    rebuild_layer_list();
  });
  connect(&AppSettings::instance(), &AppSettings::accentChanged, this, [this](const QString &) {
    apply_desktop_theme();
    refresh_theme_icons();
    stroke_preview_cache_.clear();
    rebuild_preset_dock();
  });
  if (auto *hints = QGuiApplication::styleHints()) {
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
      if (AppSettings::instance().theme() == ThemeMode::SYSTEM) {
        apply_desktop_theme();
        refresh_theme_icons();
      }
    });
  }
}

void MainWindow::rebuild_recent_menu() {
  if (recent_menu_ == nullptr) return;
  recent_menu_->clear();
  const QStringList entries = AppSettings::instance().recent_files();
  if (entries.isEmpty()) {
    auto *empty_act = recent_menu_->addAction(tr("No Recent Files"));
    empty_act->setEnabled(false);
    return;
  }
  for (const QString &entry : entries) {
    auto *act = recent_menu_->addAction(QFileInfo(entry).fileName());
    act->setToolTip(entry);
    connect(act, &QAction::triggered, this, [this, entry]() { open_file(entry.toStdString()); });
  }
  recent_menu_->addSeparator();
  recent_menu_->addAction(tr("Clear Recent Files"), this,
                          []() { AppSettings::instance().clear_recent_files(); });
}

void MainWindow::apply_pressure_curve_from_settings() {
  bool ok = false;
  const double gamma = AppSettings::instance().pressure_curve().toDouble(&ok);
  canvas_->set_pressure_curve((ok && gamma > 0.0) ? gamma : 1.0);
}

MainWindow::~MainWindow() = default;

void MainWindow::changeEvent(QEvent *event) {
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
    refresh_theme_icons();
    if (canvas_) {
      canvas_->set_dark_theme(palette().color(QPalette::Window).lightness() < 128);
    }
  }
  QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (QGuiApplication::platformName() != "offscreen") {
    QSettings ws("io.github.kreart", "Kreart");
    ws.setValue("window/geometry", saveGeometry());
    ws.setValue("window/state", saveState());
    ws.setValue("window/studio_tab", studio_tabs_ ? studio_tabs_->currentIndex() : 0);
  }
  if (document_.modified() && QGuiApplication::platformName() != "offscreen") {
    const auto btn = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("The document has unsaved modifications. Do you want to save before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (btn == QMessageBox::Save) {
      on_save_document();
      event->accept();
    } else if (btn == QMessageBox::Discard) {
      event->accept();
    } else {
      event->ignore();
    }
  } else {
    event->accept();
  }
}
static bool is_input_or_dialog_target(QObject *watched) {
  if (QApplication::activeModalWidget() != nullptr) return true;
  auto check_widget = [](QWidget *w) -> bool {
    if (!w) return false;
    if (qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
        qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w)) {
      return true;
    }
    QWidget *p = w;
    while (p) {
      if (qobject_cast<QDialog *>(p)) return true;
      p = p->parentWidget();
    }
    return false;
  };
  if (auto *w = qobject_cast<QWidget *>(watched)) {
    if (check_widget(w)) return true;
  }
  return check_widget(QApplication::focusWidget());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  // All keyboard dispatch flows through QAction shortcuts (setup_actions /
  // apply_shortcuts). The eventFilter stays installed only for future non-key
  // interception.
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
    for (const QUrl &url : event->mimeData()->urls()) {
      if (url.isLocalFile()) {
        event->acceptProposedAction();
        return;
      }
    }
  }
  QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event) {
  if (event->mimeData() != nullptr) {
    for (const QUrl &url : event->mimeData()->urls()) {
      if (!url.isLocalFile()) continue;
      const std::filesystem::path path = url.toLocalFile().toStdString();
      const std::string ext = path.extension().string();
      if (ext == ".kra" || ext == ".png") {
        open_file(path);
        event->acceptProposedAction();
        return;
      }
    }
  }
  QMainWindow::dropEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
  // Configurable shortcuts (tools, undo, rotate, zoom, mirror, quick menu) are
  // driven by QAction shortcuts. The canvas-local keys (Enter/Escape/brackets,
  // which own move_auto_selected_) are handled by Canvas::keyPressEvent; route
  // unhandled events to the canvas so they reach it even when the window itself
  // (not the canvas widget) was the event target.
  if (canvas_ && !is_input_or_dialog_target(nullptr)) {
    QApplication::sendEvent(canvas_, event);
    return;
  }
  QMainWindow::keyPressEvent(event);
}


void MainWindow::setup_actions() {
  // Tool rail actions
  tool_group_ = new QActionGroup(this);
  tool_group_->setExclusive(true);

  auto make_tool_act = [this](const QString &text, const QKeySequence &shortcut, Canvas::Tool t) {
    auto *act = new QAction(text, this);
    act->setCheckable(true);
    act->setShortcut(shortcut);
    tool_group_->addAction(act);
    connect(act, &QAction::triggered, this, [this, t]() { set_tool(t); });
    return act;
  };

  brush_act_ = make_tool_act(tr("Brush"), QKeySequence(Qt::Key_B), Canvas::Tool::BRUSH);
  brush_act_->setChecked(true);
  eraser_act_ = make_tool_act(tr("Eraser"), QKeySequence("Shift+E"), Canvas::Tool::ERASER);
  smudge_act_ = make_tool_act(tr("Smudge"), QKeySequence(Qt::Key_S), Canvas::Tool::SMUDGE);
  rect_act_ = make_tool_act(tr("Rect Select"), QKeySequence(Qt::Key_R), Canvas::Tool::RECT_SELECT);
  ellipse_act_ = make_tool_act(tr("Ellipse Select"), QKeySequence(Qt::Key_E), Canvas::Tool::ELLIPSE_SELECT);
  lasso_act_ = make_tool_act(tr("Lasso Select"), QKeySequence(Qt::Key_L), Canvas::Tool::LASSO_SELECT);
  wand_act_ = make_tool_act(tr("Magic Wand"), QKeySequence(Qt::Key_W), Canvas::Tool::MAGIC_WAND);
  move_act_ = make_tool_act(tr("Move Tool"), QKeySequence(Qt::Key_T), Canvas::Tool::MOVE);
  picker_act_ = make_tool_act(tr("Color Picker"), QKeySequence(Qt::Key_P), Canvas::Tool::COLOR_PICKER);
  fill_act_ = make_tool_act(tr("Fill Tool"), QKeySequence(Qt::Key_F), Canvas::Tool::FILL);

  swap_act_ = new QAction(tr("Swap Brush/Eraser"), this);
  swap_act_->setShortcut(QKeySequence(Qt::Key_X));
  connect(swap_act_, &QAction::triggered, this, [this]() {
    if (canvas_->tool() == Canvas::Tool::ERASER) {
      set_tool(Canvas::Tool::BRUSH);
    } else {
      set_tool(Canvas::Tool::ERASER);
    }
  });

  sym_v_act_ = new QAction(tr("Vertical Symmetry"), this);
  sym_v_act_->setCheckable(true);
  sym_v_act_->setShortcut(QKeySequence("Ctrl+Shift+M"));
  connect(sym_v_act_, &QAction::toggled, canvas_, &Canvas::set_symmetry_vertical);

  sym_h_act_ = new QAction(tr("Horizontal Symmetry"), this);
  sym_h_act_->setCheckable(true);
  sym_h_act_->setShortcut(QKeySequence("Ctrl+Shift+H"));
  connect(sym_h_act_, &QAction::toggled, canvas_, &Canvas::set_symmetry_horizontal);

  // Undo / Redo actions
  undo_act_ = new QAction(tr("Undo"), this);
  undo_act_->setShortcut(QKeySequence::Undo);
  connect(undo_act_, &QAction::triggered, this, &MainWindow::on_undo);

  redo_act_ = new QAction(tr("Redo"), this);
  redo_act_->setShortcut(QKeySequence::Redo);
  connect(redo_act_, &QAction::triggered, this, &MainWindow::on_redo);

  save_act_ = new QAction(tr("Save"), this);
  save_act_->setShortcut(QKeySequence::Save);
  connect(save_act_, &QAction::triggered, this, &MainWindow::on_save_document);

  pixelated_act_ = new QAction(tr("Pixelated View"), this);
  pixelated_act_->setCheckable(true);
  connect(pixelated_act_, &QAction::toggled, this, &MainWindow::on_pixelated_toggled);

  grid_act_ = new QAction(tr("Show Grid"), this);
  grid_act_->setCheckable(true);
  canvas_->set_show_grid(AppSettings::instance().show_grid());
  canvas_->set_grid_size(AppSettings::instance().grid_size());
  const QColor gc = AppSettings::instance().grid_color();
  canvas_->set_grid_color(core::Rgba8{static_cast<uint8_t>(gc.red()), static_cast<uint8_t>(gc.green()), static_cast<uint8_t>(gc.blue()), static_cast<uint8_t>(gc.alpha())});
  grid_act_->setChecked(canvas_->show_grid());
  connect(grid_act_, &QAction::toggled, this, [this](bool checked) {
    canvas_->set_show_grid(checked);
    AppSettings::instance().set_show_grid(checked);
  });

  // --- Shortcut-configurable actions for keys that used to be hardcoded. ---
  auto make_id_act = [this](const char *id, const char *label_text) {
    auto *act = new QAction(tr(label_text), this);
    act->setData(QString::fromUtf8(id));
    connect(act, &QAction::triggered, this, [this, id]() { on_shortcut_action(QString::fromUtf8(id)); });
    action_by_id_[id] = act;
    return act;
  };
  auto reg = [this](const char *id, QAction *act) { action_by_id_[id] = act; };
  reg("tool.brush", brush_act_);
  reg("tool.eraser", eraser_act_);
  reg("tool.smudge", smudge_act_);
  reg("tool.rect_select", rect_act_);
  reg("tool.ellipse_select", ellipse_act_);
  reg("tool.lasso", lasso_act_);
  reg("tool.wand", wand_act_);
  reg("tool.move", move_act_);
  reg("tool.picker", picker_act_);
  reg("tool.fill", fill_act_);
  reg("tool.swap_brush_eraser", swap_act_);
  reg("edit.undo", undo_act_);
  reg("edit.redo", redo_act_);

  auto *quick_menu_act = make_id_act("edit.quick_menu", "Popup Quick Menu");
  quick_menu_act->setShortcutContext(Qt::ApplicationShortcut);
  make_id_act("view.rotate_left", "Rotate Canvas Left");
  make_id_act("view.rotate_right", "Rotate Canvas Right");
  make_id_act("view.reset_rotation", "Reset Canvas Rotation");
  make_id_act("view.mirror_horizontal", "Mirror Horizontal");
  // Note: bracket/plus/minus and Enter/Escape/Delete remain canvas-local
  // (Canvas::keyPressEvent owns move_auto_selected_ semantics).

  this->addAction(quick_menu_act);
  for (auto &[id, act] : action_by_id_) {
    this->addAction(act);
  }
  apply_shortcuts();
  connect(&AppSettings::instance(), &AppSettings::shortcutsChanged, this, &MainWindow::apply_shortcuts);
}

void MainWindow::apply_shortcuts() {
  for (const auto &def : default_shortcuts()) {
    auto it = action_by_id_.find(def.id);
    if (it == action_by_id_.end()) continue;
    QAction *act = it->second;
    act->setShortcutContext(Qt::ApplicationShortcut);
    const QKeySequence seq = AppSettings::instance().shortcut(QString::fromUtf8(def.id), def.default_seq);
    if (std::strcmp(def.id, "edit.redo") == 0 &&
        (seq == QKeySequence(QKeySequence::Redo) || seq == QKeySequence("Ctrl+Shift+Z") || seq == QKeySequence("Ctrl+Y"))) {
      act->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
    } else {
      act->setShortcut(seq);
    }
  }
}

void MainWindow::on_shortcut_action(const QString &id) {
  if (id == "view.rotate_left") { on_rotate_left(); return; }
  if (id == "view.rotate_right") { on_rotate_right(); return; }
  if (id == "view.reset_rotation") { on_reset_rotation(); return; }
  if (id == "view.mirror_horizontal") { on_mirror_horizontal(); return; }
  if (id == "edit.quick_menu") { popup_quick_menu(); return; }
}

void MainWindow::on_configure_shortcuts() {
  ShortcutDialog dlg(this);
  dlg.exec();
}

void MainWindow::reset_layout() {
  if (tool_rail_) {
    addToolBar(Qt::LeftToolBarArea, tool_rail_);
    tool_rail_->setOrientation(Qt::Vertical);
    tool_rail_->show();
  }
  if (studio_dock_) {
    addDockWidget(Qt::RightDockWidgetArea, studio_dock_);
    studio_dock_->show();
  }
  if (studio_tabs_) studio_tabs_->setCurrentIndex(0);
  QSettings ws("io.github.kreart", "Kreart");
  ws.remove("window/state");
  ws.remove("window/studio_tab");
  update();
}

void MainWindow::setup_menus() {
  auto *mb = menuBar();

  // File Menu
  auto *file_menu = mb->addMenu(tr("&File"));
  auto *new_act = file_menu->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::on_new_document);
  auto *open_act = file_menu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::on_open_document);
  recent_menu_ = file_menu->addMenu(tr("Open &Recent"));
  file_menu->addAction(tr("Project &Gallery..."), QKeySequence("Ctrl+G"), this, &MainWindow::on_show_gallery);
  file_menu->addAction(save_act_);
  auto *save_as_act = file_menu->addAction(tr("Save &As..."), QKeySequence("Ctrl+Shift+S"), this, &MainWindow::on_save_as_document);
  file_menu->addSeparator();
  file_menu->addAction(tr("&Export PNG..."), QKeySequence("Ctrl+E"), this, &MainWindow::on_export_png);
  file_menu->addAction(tr("&Import Image as Layer..."), this, &MainWindow::on_import_image_as_layer);
  file_menu->addAction(tr("Import &Brush Pack..."), this, &MainWindow::on_import_brush_pack);
  file_menu->addAction(tr("Recover Damaged &Project..."), this, &MainWindow::on_recover_damaged_project);
  auto *tl_menu = file_menu->addMenu(tr("&Timelapse"));
  tl_menu->addAction(tr("Replay && &Export..."), this, &MainWindow::on_replay_timelapse);
  file_menu->addAction(tr("Import Shortcut &Profile..."), this, [this]() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Shortcut Profile"), QString(), tr("Kreart Shortcut Profile (*.ksf)"));
    if (!path.isEmpty()) {
      if (AppSettings::instance().import_shortcuts(path)) {
        set_status(tr("Imported shortcuts from %1").arg(QFileInfo(path).fileName()));
      }
    }
  });
  file_menu->addAction(tr("Export Shortcut &Profile..."), this, [this]() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Shortcut Profile"), QString(), tr("Kreart Shortcut Profile (*.ksf)"));
    if (!path.isEmpty()) {
      if (AppSettings::instance().export_shortcuts(path)) {
        set_status(tr("Exported shortcuts to %1").arg(QFileInfo(path).fileName()));
      }
    }
  });
  file_menu->addSeparator();
  file_menu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  // Edit Menu
  auto *edit_menu = mb->addMenu(tr("&Edit"));
  edit_menu->addAction(undo_act_);
  edit_menu->addAction(redo_act_);
  edit_menu->addSeparator();
  edit_menu->addAction(tr("Cu&t"), QKeySequence::Cut, this, &MainWindow::on_cut);
  edit_menu->addAction(tr("&Copy"), QKeySequence::Copy, this, &MainWindow::on_copy);
  edit_menu->addAction(tr("&Paste"), QKeySequence::Paste, this, &MainWindow::on_paste);
  edit_menu->addAction(tr("Paste as &New Layer"), QKeySequence("Ctrl+Shift+V"), this, &MainWindow::on_paste_as_new_layer);
  edit_menu->addSeparator();
  edit_menu->addAction(tr("&Deselect"), QKeySequence("Ctrl+D"), this, &MainWindow::on_deselect);
  edit_menu->addAction(tr("&Invert Selection"), QKeySequence("Ctrl+Shift+I"), this, &MainWindow::on_invert_selection);
  edit_menu->addAction(tr("Select &All"), QKeySequence::SelectAll, this, &MainWindow::on_select_all);
  edit_menu->addAction(tr("&Clear Selection"), QKeySequence::Delete, this, &MainWindow::on_clear_selection);
  edit_menu->addSeparator();
  edit_menu->addAction(tr("Keyboard &Shortcuts..."), this, &MainWindow::on_configure_shortcuts);

  // Select Menu
  auto *select_menu = mb->addMenu(tr("&Select"));
  select_menu->addAction(tr("&All"), QKeySequence::SelectAll, this, &MainWindow::on_select_all);
  select_menu->addAction(tr("&Deselect"), QKeySequence("Ctrl+D"), this, &MainWindow::on_deselect);
  select_menu->addAction(tr("&Invert"), QKeySequence("Ctrl+Shift+I"), this, &MainWindow::on_invert_selection);
  select_menu->addSeparator();
  select_menu->addAction(tr("&Feather..."), this, &MainWindow::on_feather_selection);
  select_menu->addAction(tr("&Grow..."), this, &MainWindow::on_grow_selection);
  select_menu->addAction(tr("&Shrink..."), this, &MainWindow::on_shrink_selection);
  select_menu->addAction(tr("&Border..."), this, &MainWindow::on_border_selection);

  // View Menu
  auto *view_menu = mb->addMenu(tr("&View"));
  {
    auto *zin = view_menu->addAction(tr("Zoom &In"), this, &MainWindow::on_zoom_in);
    action_by_id_["view.zoom_in"] = zin; this->addAction(zin);
    auto *zout = view_menu->addAction(tr("Zoom &Out"), this, &MainWindow::on_zoom_out);
    action_by_id_["view.zoom_out"] = zout; this->addAction(zout);
    auto *zfit = view_menu->addAction(tr("Zoom to &Fit"), this, &MainWindow::on_zoom_fit);
    action_by_id_["view.zoom_fit"] = zfit; this->addAction(zfit);
  }
  view_menu->addSeparator();
  view_menu->addAction(tr("Rotate Left (15°)"), QKeySequence("Ctrl+["), this, &MainWindow::on_rotate_left);
  view_menu->addAction(tr("Rotate Right (15°)"), QKeySequence("Ctrl+]"), this, &MainWindow::on_rotate_right);
  view_menu->addAction(tr("Reset Rotation"), QKeySequence("Ctrl+Shift+R"), this, &MainWindow::on_reset_rotation);
  view_menu->addSeparator();
  view_menu->addAction(tr("Mirror &Horizontal"), QKeySequence(Qt::Key_M), this, &MainWindow::on_mirror_horizontal);
  view_menu->addAction(tr("Mirror &Vertical"), QKeySequence("Ctrl+M"), this, &MainWindow::on_mirror_vertical);
  view_menu->addSeparator();
  view_menu->addAction(pixelated_act_);
  view_menu->addAction(grid_act_);
  view_menu->addAction(tr("&Configure Grid..."), this, &MainWindow::on_grid_settings);
  view_menu->addSeparator();
  reset_layout_act_ = view_menu->addAction(tr("&Reset Layout"), this, &MainWindow::reset_layout);
  reset_layout_act_->setToolTip(tr("Restore the default Tool (left) and Studio (right) arrangement."));

  // Image Menu
  auto *img_menu = mb->addMenu(tr("&Image"));
  img_menu->addAction(tr("&Canvas Size..."), this, &MainWindow::on_canvas_size);
  img_menu->addAction(tr("&Scale Image (Resample)..."), this, &MainWindow::on_scale_image);

  // Layer Menu
  auto *layer_menu = mb->addMenu(tr("&Layer"));
  layer_menu->addAction(tr("&New Layer"), QKeySequence("Ctrl+Shift+N"), this, &MainWindow::on_add_layer);
  layer_menu->addAction(tr("New &Group"), this, &MainWindow::on_add_group);
  clip_mask_act_ = layer_menu->addAction(tr("Toggle &Clipping Mask"), QKeySequence("Ctrl+Alt+G"), this, &MainWindow::on_toggle_clipping_mask);
  layer_menu->addAction(tr("&Merge Down"), QKeySequence("Ctrl+Shift+E"), this, &MainWindow::on_merge_down);
  layer_menu->addAction(tr("&Delete Layer"), this, &MainWindow::on_remove_layer);
  layer_menu->addAction(tr("&Rename Layer..."), this, &MainWindow::on_rename_layer);

  // Filter Menu
  auto *filter_menu = mb->addMenu(tr("F&ilter"));
  filter_menu->addAction(tr("&Hue / Saturation / Value..."), QKeySequence("Ctrl+U"), this, &MainWindow::on_filter_hsv);
  filter_menu->addAction(tr("&Brightness && Contrast..."), this, &MainWindow::on_filter_brightness_contrast);
  filter_menu->addAction(tr("&Color &Balance..."), QKeySequence("Ctrl+B"), this, &MainWindow::on_filter_color_balance);
  filter_menu->addAction(tr("&Gaussian Blur..."), this, &MainWindow::on_filter_gaussian_blur);

  // Settings Menu
  auto *settings_menu = mb->addMenu(tr("&Settings"));
  settings_menu->addAction(tr("&Grid && Guidelines Setup..."), this, &MainWindow::on_grid_settings);
  settings_menu->addAction(tr("&Preferences..."), this, &MainWindow::on_preferences);
}

void MainWindow::setup_tool_rail() {
  tool_rail_ = new QToolBar(tr("Tools"), this);
  tool_rail_->setObjectName("toolRail");
  tool_rail_->setOrientation(Qt::Vertical);
  // Fixed to the left side (no movable handle, no wobble/shift).
  tool_rail_->setMovable(false);
  tool_rail_->setAllowedAreas(Qt::LeftToolBarArea);
  tool_rail_->setIconSize(QSize(22, 22));
  tool_rail_->setStyleSheet("QToolBar { border: none; background: transparent; spacing: 2px; } "
                            "QToolButton { border: none; border-radius: 4px; padding: 4px; margin: 1px; } "
                            "QToolButton:hover { background: rgba(255, 255, 255, 0.08); } "
                            "QToolButton:checked { background: rgba(255, 255, 255, 0.16); }");
  addToolBar(Qt::LeftToolBarArea, tool_rail_);

  tool_rail_->addAction(brush_act_);
  tool_rail_->addAction(eraser_act_);
  tool_rail_->addAction(smudge_act_);
  tool_rail_->addAction(rect_act_);
  tool_rail_->addAction(ellipse_act_);
  tool_rail_->addAction(lasso_act_);
  tool_rail_->addAction(wand_act_);
  auto *lasso_btn = qobject_cast<QToolButton *>(tool_rail_->widgetForAction(lasso_act_));
  if (lasso_btn) {
    auto *lasso_menu = new QMenu(this);
    auto *freehand_act = lasso_menu->addAction(tr("Freehand Lasso"));
    auto *magic_act = lasso_menu->addAction(tr("Magic Lasso (Auto-Catch Object)"));
    connect(freehand_act, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->set_lasso_mode(Canvas::LassoMode::FREEHAND);
      lasso_act_->setToolTip(tr("Lasso Select (Freehand)"));
      set_status(tr("Freehand Lasso selected"));
    });
    connect(magic_act, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->set_lasso_mode(Canvas::LassoMode::MAGIC_OBJECT);
      lasso_act_->setToolTip(tr("Magic Lasso (Auto-Catch Object)"));
      set_status(tr("Magic Lasso selected (auto-catches object shape)"));
    });
    lasso_btn->setMenu(lasso_menu);
    lasso_btn->setPopupMode(QToolButton::DelayedPopup);
  }
  tool_rail_->addAction(move_act_);
  tool_rail_->addAction(picker_act_);
  tool_rail_->addAction(fill_act_);
  tool_rail_->addSeparator();
  tool_rail_->addAction(swap_act_);
  tool_rail_->addSeparator();
  tool_rail_->addAction(sym_v_act_);
  tool_rail_->addAction(sym_h_act_);
}

void MainWindow::setup_studio_dock() {
  studio_dock_ = new QDockWidget(tr("Studio"), this);
  studio_dock_->setObjectName("studioDock");
  studio_dock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
  studio_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
  studio_tabs_ = new QTabWidget(studio_dock_);
  studio_tabs_->setTabPosition(QTabWidget::North);

  // --------------------------------------------------------------------------
  // Tab 1: Presets Dock
  // --------------------------------------------------------------------------
  auto *presets_page = new QWidget(studio_tabs_);
  auto *presets_layout = new QVBoxLayout(presets_page);
  presets_layout->setContentsMargins(8, 8, 8, 8);
  presets_layout->setSpacing(6);

  auto *presets_top_row = new QHBoxLayout();
  preset_category_combo_ = new QComboBox(presets_page);
  preset_category_combo_->setObjectName("presetCategoryCombo");
  preset_category_combo_->addItems({"All", "Erasers", "Sketch", "Ink", "Digital", "Paint", "Textures", "FX"});
  connect(preset_category_combo_, &QComboBox::currentIndexChanged, this, &MainWindow::on_preset_category_changed);
  presets_top_row->addWidget(preset_category_combo_, 1);

  preset_view_toggle_btn_ = new QPushButton(presets_page);
  preset_view_toggle_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/view-list-symbolic.svg", palette(), 16));
  preset_view_toggle_btn_->setToolTip(tr("Switch to List View"));
  connect(preset_view_toggle_btn_, &QPushButton::clicked, this, &MainWindow::toggle_preset_view_mode);
  presets_top_row->addWidget(preset_view_toggle_btn_);
  presets_layout->addLayout(presets_top_row);

  preset_search_edit_ = new QLineEdit(presets_page);
  preset_search_edit_->setPlaceholderText(tr("Search brushes..."));
  connect(preset_search_edit_, &QLineEdit::textChanged, this, &MainWindow::on_preset_search_changed);
  presets_layout->addWidget(preset_search_edit_);

  preset_list_ = new QListWidget(presets_page);
  preset_list_->setViewMode(QListView::ListMode);
  preset_list_->setIconSize(QSize(200, 32));
  preset_list_->setSpacing(3);
  connect(preset_list_, &QListWidget::currentRowChanged, this, [this](int row) {
    QListWidgetItem *item = (row >= 0) ? preset_list_->item(row) : nullptr;
    if (!item) return;
    const std::size_t db_idx = static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong());
    if (db_idx < preset_database_.presets().size()) select_preset(db_idx);
  });
  presets_layout->addWidget(preset_list_);
  studio_tabs_->addTab(presets_page, IconHelper::load_symbolic(":/icons/icons/tool-brush-symbolic.svg", palette(), 16), tr("Brushes"));

  // --------------------------------------------------------------------------
  // Tab 2: Color Studio
  // --------------------------------------------------------------------------
  auto *color_page = new QWidget(studio_tabs_);
  auto *color_layout = new QVBoxLayout(color_page);
  color_layout->setContentsMargins(8, 8, 8, 8);
  color_layout->setSpacing(8);

  color_well_btn_ = new QPushButton(color_page);
  color_well_btn_->setFixedHeight(36);
  color_well_btn_->setStyleSheet("background-color: black; border-radius: 6px;");
  color_layout->addWidget(color_well_btn_);

  // Recent 8 Swatches
  auto *recent_lbl = new QLabel(tr("Recent Colors:"), color_page);
  color_layout->addWidget(recent_lbl);

  auto *recent_row = new QHBoxLayout();
  recent_row->setSpacing(4);
  for (int i = 0; i < 8; ++i) {
    auto *btn = new QPushButton(color_page);
    btn->setFixedSize(28, 28);
    btn->setStyleSheet("background-color: transparent; border: 1px dashed gray; border-radius: 4px;");
    recent_swatch_buttons_.push_back(btn);
    recent_row->addWidget(btn);
  }
  color_layout->addLayout(recent_row);

  // Profile Swatches
  auto *profile_row = new QHBoxLayout();
  profile_combo_ = new QComboBox(color_page);
  for (const auto &prof : ColorHistory::palette_profiles()) {
    profile_combo_->addItem(QString::fromStdString(prof.name));
  }
  connect(profile_combo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
    color_history_.set_active_profile_index(idx);
    apply_brush_color(canvas_->brush_settings().color, false);
  });
  profile_row->addWidget(profile_combo_);
  color_layout->addLayout(profile_row);

  auto *prof_swatches_row = new QHBoxLayout();
  prof_swatches_row->setSpacing(4);
  for (int i = 0; i < 8; ++i) {
    auto *btn = new QPushButton(color_page);
    btn->setFixedSize(28, 28);
    profile_swatch_buttons_.push_back(btn);
    prof_swatches_row->addWidget(btn);
  }
  color_layout->addLayout(prof_swatches_row);

  // Studio Color Picker
  studio_picker_ = new StudioColorPicker(core::Rgba8{0, 0, 0, 255}, color_page);
  connect(studio_picker_, &StudioColorPicker::colorChanged, this, [this](const core::Rgba8 &c) {
    apply_brush_color(c, false);
  });
  connect(studio_picker_, &StudioColorPicker::colorCommitted, this, [this](const core::Rgba8 &c) {
    apply_brush_color(c, true);
  });
  color_layout->addWidget(studio_picker_, 0, Qt::AlignCenter);

  auto *clear_hist_btn = new QPushButton(tr("Clear History"), color_page);
  connect(clear_hist_btn, &QPushButton::clicked, this, [this]() {
    color_history_.clear();
    apply_brush_color(canvas_->brush_settings().color, false);
  });
  color_layout->addWidget(clear_hist_btn);
  color_layout->addStretch(1);
  studio_tabs_->addTab(color_page, IconHelper::load_symbolic(":/icons/icons/tool-picker-symbolic.svg", palette(), 16), tr("Color"));

  // --------------------------------------------------------------------------
  // Tab 3: Brush Dynamics & Tool Settings
  // --------------------------------------------------------------------------
  auto *tool_page = new QWidget(studio_tabs_);
  auto *tool_layout = new QVBoxLayout(tool_page);
  tool_layout->setContentsMargins(8, 8, 8, 8);
  tool_layout->setSpacing(8);

  // Size
  auto *size_row = new QHBoxLayout();
  size_row->addWidget(new QLabel(tr("Size:"), tool_page));
  size_slider_ = new QSlider(Qt::Horizontal, tool_page);
  size_slider_->setRange(1, 500);
  size_slider_->setValue(24);
  size_spin_ = new QSpinBox(tool_page);
  size_spin_->setRange(1, 500);
  size_spin_->setValue(24);
  connect(size_slider_, &QSlider::valueChanged, size_spin_, &QSpinBox::setValue);
  connect(size_spin_, &QSpinBox::valueChanged, size_slider_, &QSlider::setValue);
  connect(size_spin_, &QSpinBox::valueChanged, this, &MainWindow::on_brush_size_changed);
  size_row->addWidget(size_slider_);
  size_row->addWidget(size_spin_);
  tool_layout->addLayout(size_row);

  // Opacity
  auto *opac_row = new QHBoxLayout();
  opac_row->addWidget(new QLabel(tr("Opacity:"), tool_page));
  opacity_slider_ = new QSlider(Qt::Horizontal, tool_page);
  opacity_slider_->setRange(1, 100);
  opacity_slider_->setValue(100);
  connect(opacity_slider_, &QSlider::valueChanged, this, &MainWindow::on_brush_opacity_changed);
  opac_row->addWidget(opacity_slider_);
  tool_layout->addLayout(opac_row);

  // Smoothing
  auto *smooth_row = new QHBoxLayout();
  smooth_row->addWidget(new QLabel(tr("Smoothing:"), tool_page));
  smoothing_slider_ = new QSlider(Qt::Horizontal, tool_page);
  smoothing_slider_->setRange(0, 100);
  smoothing_slider_->setValue(50);
  connect(smoothing_slider_, &QSlider::valueChanged, this, &MainWindow::on_brush_smoothing_changed);
  smooth_row->addWidget(smoothing_slider_);
  tool_layout->addLayout(smooth_row);

  auto *smooth_mode_row = new QHBoxLayout();
  smooth_mode_row->addWidget(new QLabel(tr("Mode:"), tool_page));
  smoothing_mode_combo_ = new QComboBox(tool_page);
  smoothing_mode_combo_->addItems({tr("None"), tr("Basic"), tr("Weighted"), tr("Stabilizer")});
  smoothing_mode_combo_->setCurrentIndex(2); // Weighted
  connect(smoothing_mode_combo_, &QComboBox::currentIndexChanged, this, &MainWindow::on_smoothing_mode_changed);
  smooth_mode_row->addWidget(smoothing_mode_combo_);
  tool_layout->addLayout(smooth_mode_row);

  // Tilt dynamics
  tilt_check_ = new QCheckBox(tr("Tilt Dynamics"), tool_page);
  connect(tilt_check_, &QCheckBox::toggled, canvas_, &Canvas::set_tilt_enabled);
  tool_layout->addWidget(tilt_check_);

  // Texture controls
  auto *texture_group = new QGroupBox(tr("Texture Mask"), tool_page);
  auto *tex_layout = new QVBoxLayout(texture_group);
  texture_check_ = new QCheckBox(tr("Enable Texture"), texture_group);
  connect(texture_check_, &QCheckBox::toggled, canvas_, &Canvas::set_texture_enabled);
  tex_layout->addWidget(texture_check_);

  texture_pattern_combo_ = new QComboBox(texture_group);
  texture_pattern_combo_->addItems({tr("Rough Paper"), tr("Canvas Grain"), tr("Charcoal Tooth")});
  connect(texture_pattern_combo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
    if (idx == 0) canvas_->set_texture_pattern(brush::TextureMask::create_rough_paper(128));
    else if (idx == 1) canvas_->set_texture_pattern(brush::TextureMask::create_canvas_grain(128));
    else canvas_->set_texture_pattern(brush::TextureMask::create_charcoal_tooth(128));
  });
  tex_layout->addWidget(texture_pattern_combo_);

  auto *scale_row = new QHBoxLayout();
  scale_row->addWidget(new QLabel(tr("Scale:"), texture_group));
  texture_scale_slider_ = new QSlider(Qt::Horizontal, texture_group);
  texture_scale_slider_->setObjectName("textureScaleSlider");
  texture_scale_slider_->setRange(5, 500);
  texture_scale_slider_->setValue(100);
  connect(texture_scale_slider_, &QSlider::valueChanged, this,
          [this](int v) { canvas_->set_texture_scale(v / 100.0); });
  scale_row->addWidget(texture_scale_slider_);
  tex_layout->addLayout(scale_row);

  auto *strength_row = new QHBoxLayout();
  strength_row->addWidget(new QLabel(tr("Strength:"), texture_group));
  texture_strength_slider_ = new QSlider(Qt::Horizontal, texture_group);
  texture_strength_slider_->setObjectName("textureStrengthSlider");
  texture_strength_slider_->setRange(0, 100);
  texture_strength_slider_->setValue(75);
  connect(texture_strength_slider_, &QSlider::valueChanged, this,
          [this](int v) { canvas_->set_texture_strength(v / 100.0); });
  strength_row->addWidget(texture_strength_slider_);
  tex_layout->addLayout(strength_row);

  tool_layout->addWidget(texture_group);

  // Smudge controls (shown when Smudge tool active)
  smudge_controls_widget_ = new QWidget(tool_page);
  auto *smudge_layout = new QVBoxLayout(smudge_controls_widget_);
  smudge_layout->setContentsMargins(0, 0, 0, 0);

  auto *smudge_rate_row = new QHBoxLayout();
  smudge_rate_row->addWidget(new QLabel(tr("Smudge Rate:"), smudge_controls_widget_));
  smudge_rate_slider_ = new QSlider(Qt::Horizontal, smudge_controls_widget_);
  smudge_rate_slider_->setRange(0, 100);
  smudge_rate_slider_->setValue(80);
  connect(smudge_rate_slider_, &QSlider::valueChanged, this, [this](int val) {
    canvas_->set_brush_smudge_rate(val / 100.0);
  });
  smudge_rate_row->addWidget(smudge_rate_slider_);
  smudge_layout->addLayout(smudge_rate_row);

  auto *color_rate_row = new QHBoxLayout();
  color_rate_row->addWidget(new QLabel(tr("Color Rate:"), smudge_controls_widget_));
  color_rate_slider_ = new QSlider(Qt::Horizontal, smudge_controls_widget_);
  color_rate_slider_->setRange(0, 100);
  color_rate_slider_->setValue(20);
  connect(color_rate_slider_, &QSlider::valueChanged, this, [this](int val) {
    canvas_->set_brush_color_rate(val / 100.0);
  });
  color_rate_row->addWidget(color_rate_slider_);
  smudge_layout->addLayout(color_rate_row);

  smudge_controls_widget_->setVisible(false);
  tool_layout->addWidget(smudge_controls_widget_);

  // Magic wand controls (shown when Magic Wand tool active)
  wand_controls_widget_ = new QWidget(tool_page);
  auto *wand_layout = new QVBoxLayout(wand_controls_widget_);
  wand_layout->setContentsMargins(0, 0, 0, 0);
  auto *tol_row = new QHBoxLayout();
  tol_row->addWidget(new QLabel(tr("Wand Tolerance:"), wand_controls_widget_));
  wand_tolerance_slider_ = new QSlider(Qt::Horizontal, wand_controls_widget_);
  wand_tolerance_slider_->setObjectName("wandToleranceSlider");
  wand_tolerance_slider_->setRange(0, 255);
  wand_tolerance_slider_->setValue(canvas_->wand_tolerance());
  connect(wand_tolerance_slider_, &QSlider::valueChanged, canvas_, &Canvas::set_wand_tolerance);
  tol_row->addWidget(wand_tolerance_slider_);
  wand_layout->addLayout(tol_row);
  wand_contiguous_check_ = new QCheckBox(tr("Contiguous"), wand_controls_widget_);
  wand_contiguous_check_->setObjectName("wandContiguousCheck");
  wand_contiguous_check_->setChecked(canvas_->wand_contiguous());
  connect(wand_contiguous_check_, &QCheckBox::toggled, canvas_, &Canvas::set_wand_contiguous);
  wand_layout->addWidget(wand_contiguous_check_);
  wand_controls_widget_->setVisible(false);
  tool_layout->addWidget(wand_controls_widget_);

  tool_layout->addStretch(1);
  studio_tabs_->addTab(tool_page, IconHelper::load_symbolic(":/icons/icons/emblem-system-symbolic.svg", palette(), 16), tr("Tool Settings"));

  // --------------------------------------------------------------------------
  // Tab 4: Layer Management
  // --------------------------------------------------------------------------
  layer_panel_ = new LayerPanel(document_, canvas_, studio_tabs_);
  layer_tree_ = layer_panel_->tree_widget();
  connect(layer_panel_, &LayerPanel::activeLayerChanged, this, [this](std::size_t) {
    update_actions();
  });
  connect(layer_panel_, &LayerPanel::layerModified, this, [this]() {
    update_actions();
  });
  studio_tabs_->addTab(layer_panel_, IconHelper::load_symbolic(":/icons/icons/layer-symbolic.svg", palette(), 16), tr("Layers"));

  studio_dock_->setWidget(studio_tabs_);
  addDockWidget(Qt::RightDockWidgetArea, studio_dock_);
}

void MainWindow::setup_status_bar() {
  auto *sb = statusBar();
  cursor_label_ = new QLabel("X: --  Y: --", this);
  pressure_label_ = new QLabel("Pressure: --", this);
  tilt_label_ = new QLabel("Tilt: --", this);
  zoom_label_ = new QLabel("100%", this);
  status_label_ = new QLabel(tr("Ready"), this);

  sb->addPermanentWidget(cursor_label_);
  sb->addPermanentWidget(pressure_label_);
  sb->addPermanentWidget(tilt_label_);
  sb->addPermanentWidget(zoom_label_);
  sb->addWidget(status_label_, 1);
}

void MainWindow::setup_quick_menu() {
  quick_menu_ = new QuickMenu(this);
  connect(quick_menu_, &QuickMenu::undoRequested, this, &MainWindow::on_undo);
  connect(quick_menu_, &QuickMenu::redoRequested, this, &MainWindow::on_redo);
  connect(quick_menu_, &QuickMenu::toggleEraserRequested, this, [this]() {
    swap_act_->trigger();
  });
  connect(quick_menu_, &QuickMenu::zoomFitRequested, this, &MainWindow::on_zoom_fit);
  connect(quick_menu_, &QuickMenu::resetRotationRequested, this, &MainWindow::on_reset_rotation);
  connect(quick_menu_, &QuickMenu::mirrorCanvasRequested, this, &MainWindow::on_mirror_horizontal);
  connect(quick_menu_, &QuickMenu::colorSelected, this, [this](const core::Rgba8 &c) {
    apply_brush_color(c, true);
  });
  connect(quick_menu_, &QuickMenu::sizeChanged, this, [this](double sz) {
    canvas_->set_brush_size(sz);
    if (size_slider_) size_slider_->setValue(static_cast<int>(sz));
  });
  connect(quick_menu_, &QuickMenu::opacityChanged, this, [this](double op) {
    canvas_->set_brush_opacity(op);
    if (opacity_slider_) opacity_slider_->setValue(static_cast<int>(std::round(op * 100.0)));
  });
  connect(quick_menu_, &QuickMenu::presetSelected, this, &MainWindow::select_preset);
}

void MainWindow::setup_autosave() {
  connect(&autosave_timer_, &QTimer::timeout, this, &MainWindow::trigger_autosave);
  autosave_timer_.start(60000); // Autosave every 60s
}

void MainWindow::trigger_autosave() {
  if (document_.modified()) {
    std::filesystem::path saved_path;
    std::string error;
    if (io::perform_autosave(document_, current_path_, saved_path, error)) {
      set_status(tr("Autosaved to %1").arg(QString::fromStdString(saved_path.filename().string())));
    } else {
      set_status(tr("Autosave failed"));
    }
  }
}

void MainWindow::popup_quick_menu(double x, double y) {
  if (!quick_menu_) return;
  quick_menu_->set_brush_color(canvas_->brush_settings().color);
  quick_menu_->set_brush_size(canvas_->brush_size());
  quick_menu_->set_brush_opacity(canvas_->brush_settings().opacity);
  quick_menu_->set_is_eraser(canvas_->erasing());
  quick_menu_->set_recent_colors(color_history_.colors());

  std::vector<QuickMenu::PresetItem> favs;
  const auto &presets = preset_database_.presets();
  for (std::size_t i = 0; i < presets.size() && i < 8; ++i) {
    favs.emplace_back(i, presets[i].name, presets[i].path,
                      stroke_preview_cache_.get_preview(presets[i], &preset_database_,
                                                        palette().color(QPalette::Window).lightness() < 128));
  }
  quick_menu_->set_presets(favs);
  if (active_preset_index_) quick_menu_->set_active_preset(*active_preset_index_);

  QPoint global_pos;
  if (x >= 0.0 && y >= 0.0) {
    global_pos = canvas_->mapToGlobal(QPoint(static_cast<int>(x), static_cast<int>(y)));
  } else {
    global_pos = QCursor::pos();
  }
  quick_menu_->popup_at(global_pos);
}

void MainWindow::refresh_theme_icons() {
  const auto &pal = palette();
  brush_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-brush-symbolic.svg", pal, 22));
  eraser_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-eraser-symbolic.svg", pal, 22));
  smudge_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-smudge-symbolic.svg", pal, 22));
  rect_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-rect-select-symbolic.svg", pal, 22));
  ellipse_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-ellipse-select-symbolic.svg", pal, 22));
  lasso_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-lasso-symbolic.svg", pal, 22));
  wand_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-wand-symbolic.svg", pal, 22));
  move_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-move-symbolic.svg", pal, 22));
  picker_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-picker-symbolic.svg", pal, 22));
  fill_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-fill-symbolic.svg", pal, 22));
  swap_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/tool-swap-symbolic.svg", pal, 22));
  sym_v_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/symmetry-v-symbolic.svg", pal, 22));
  sym_h_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/symmetry-h-symbolic.svg", pal, 22));
  undo_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/edit-undo-symbolic.svg", pal, 16));
  redo_act_->setIcon(IconHelper::load_symbolic(":/icons/icons/edit-redo-symbolic.svg", pal, 16));
}

void MainWindow::update_title() {
  const QString doc_name = current_path_.empty() ? tr("Untitled") : QString::fromStdString(current_path_.filename().string());
  const QString mod = document_.modified() ? "*" : "";
  setWindowTitle(QString("%1%2 - Kreart (%3×%4)")
                     .arg(doc_name)
                     .arg(mod)
                     .arg(document_.width())
                     .arg(document_.height()));
}

void MainWindow::update_actions() {
  if (undo_act_) undo_act_->setEnabled(document_.can_undo());
  if (redo_act_) redo_act_->setEnabled(document_.can_redo());
  if (clip_mask_act_) {
    const std::size_t idx = document_.active_layer_index();
    clip_mask_act_->setEnabled(idx < document_.layer_count() && idx > 0);
  }
}

void MainWindow::set_status(const QString &text) {
  if (status_label_) status_label_->setText(text);
}

void MainWindow::set_tool(Canvas::Tool tool) {
  canvas_->set_tool(tool);
  if (smudge_controls_widget_) {
    smudge_controls_widget_->setVisible(tool == Canvas::Tool::SMUDGE);
  }
  if (wand_controls_widget_) {
    wand_controls_widget_->setVisible(tool == Canvas::Tool::MAGIC_WAND);
  }
}

void MainWindow::on_brush_size_changed() {
  if (size_spin_) canvas_->set_brush_size(size_spin_->value());
}

void MainWindow::on_brush_opacity_changed() {
  if (opacity_slider_) canvas_->set_brush_opacity(opacity_slider_->value() / 100.0);
}

void MainWindow::on_brush_smoothing_changed() {
  if (smoothing_slider_) canvas_->set_brush_smoothing(smoothing_slider_->value() / 100.0);
}

void MainWindow::on_smoothing_mode_changed(int index) {
  static const brush::SmoothingMode modes[] = {
      brush::SmoothingMode::NONE,
      brush::SmoothingMode::BASIC,
      brush::SmoothingMode::WEIGHTED,
      brush::SmoothingMode::STABILIZER};
  if (index >= 0 && index < 4) {
    canvas_->set_smoothing_mode(modes[index]);
  }
}

void MainWindow::apply_brush_color(core::Rgba8 color, bool add_to_history) {
  canvas_->set_brush_color(color);
  if (color_well_btn_) {
    color_well_btn_->setStyleSheet(QString("background-color: rgb(%1, %2, %3); border-radius: 6px;")
                                       .arg(color.r).arg(color.g).arg(color.b));
  }
  if (studio_picker_ && (studio_picker_->color().r != color.r ||
                         studio_picker_->color().g != color.g ||
                         studio_picker_->color().b != color.b)) {
    studio_picker_->set_color(color, false);
  }
  if (add_to_history) {
    color_history_.record(color);
  }

  // Update Recent Swatches
  const auto &recent = color_history_.colors();
  for (std::size_t i = 0; i < recent_swatch_buttons_.size(); ++i) {
    if (i < recent.size()) {
      const auto &c = recent[i];
      recent_swatch_buttons_[i]->setStyleSheet(
          QString("background-color: rgb(%1, %2, %3); border: 1px solid rgba(255,255,255,0.3); border-radius: 4px;")
              .arg(c.r).arg(c.g).arg(c.b));
      recent_swatch_buttons_[i]->setEnabled(true);
      recent_swatch_buttons_[i]->disconnect();
      connect(recent_swatch_buttons_[i], &QPushButton::clicked, this, [this, c]() {
        apply_brush_color(c, false);
      });
    } else {
      recent_swatch_buttons_[i]->setStyleSheet("background-color: transparent; border: 1px dashed gray; border-radius: 4px;");
      recent_swatch_buttons_[i]->setEnabled(false);
    }
  }

  // Update Profile Swatches
  const auto &active_profile = color_history_.active_palette();
  for (std::size_t i = 0; i < profile_swatch_buttons_.size(); ++i) {
    if (i < active_profile.size()) {
      const auto &c = active_profile[i];
      profile_swatch_buttons_[i]->setStyleSheet(
          QString("background-color: rgb(%1, %2, %3); border: 1px solid rgba(255,255,255,0.3); border-radius: 4px;")
              .arg(c.r).arg(c.g).arg(c.b));
      profile_swatch_buttons_[i]->setEnabled(true);
      profile_swatch_buttons_[i]->disconnect();
      connect(profile_swatch_buttons_[i], &QPushButton::clicked, this, [this, c]() {
        apply_brush_color(c, true);
      });
    }
  }
}

void MainWindow::on_color_picked(const core::Rgba8 &color) {
  apply_brush_color(color, true);
  set_status(QString("Picked color: #%1%2%3")
                 .arg(color.r, 2, 16, QChar('0'))
                 .arg(color.g, 2, 16, QChar('0'))
                 .arg(color.b, 2, 16, QChar('0')));
}

void MainWindow::load_presets() {
  preset_database_.clear();
  preset_database_.clear_search_paths();
  const bool krita = AppSettings::instance().detect_krita_brushes();
  for (const auto &path : resources::ResourceDatabase::standard_preset_paths(krita)) {
    preset_database_.add_search_path(path);
  }
  preset_database_.add_search_path(
      resources::ResourceDatabase::user_resource_directory() / "presets");
  preset_database_.scan(true, krita);
  rebuild_preset_dock();
}

void MainWindow::rebuild_preset_dock() {
  if (!preset_list_) return;
  preset_list_->clear();

  const QString search = preset_search_edit_ ? preset_search_edit_->text().toLower() : "";
  const QString category = preset_category_combo_ ? preset_category_combo_->currentText() : "All";
  const bool is_dark = palette().color(QPalette::Window).lightness() < 128;

  // Visible entries are stored as database indices, not list rows: with a
  // category filter or search active the two differ, and select_preset()
  // indexes the full preset vector.
  const auto &all_presets = preset_database_.presets();
  std::vector<std::size_t> visible;
  if (category == "All") {
    visible.reserve(all_presets.size());
    for (std::size_t i = 0; i < all_presets.size(); ++i) visible.push_back(i);
  } else {
    const auto filtered = preset_database_.presets_for_category(category.toStdString());
    for (const auto *fp : filtered) {
      for (std::size_t i = 0; i < all_presets.size(); ++i) {
        if (fp == &all_presets[i]) {
          visible.push_back(i);
          break;
        }
      }
    }
  }

  for (std::size_t db_index : visible) {
    const auto *p = &all_presets[db_index];
    if (!search.isEmpty() && !QString::fromStdString(p->name).toLower().contains(search)) continue;

    auto *item = new QListWidgetItem(QString::fromStdString(p->name), preset_list_);
    item->setData(Qt::UserRole, static_cast<qulonglong>(db_index));
    QPixmap ribbon = stroke_preview_cache_.get_preview(*p, &preset_database_, is_dark, 200, 32);
    if (!ribbon.isNull()) {
      item->setIcon(QIcon(ribbon));
    }
    item->setSizeHint(QSize(220, 38));
  }
}

void MainWindow::toggle_preset_view_mode() {
  if (preset_view_mode_ == PresetViewMode::THUMBNAILS) {
    preset_view_mode_ = PresetViewMode::DETAILS;
    preset_list_->setViewMode(QListView::ListMode);
    preset_list_->setIconSize(QSize(180, 28));
    if (preset_view_toggle_btn_) {
      preset_view_toggle_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/view-grid-symbolic.svg", palette(), 16));
      preset_view_toggle_btn_->setToolTip(tr("Switch to Grid View"));
      preset_view_toggle_btn_->setText("");
    }
  } else {
    preset_view_mode_ = PresetViewMode::THUMBNAILS;
    preset_list_->setViewMode(QListView::IconMode);
    preset_list_->setIconSize(QSize(48, 48));
    if (preset_view_toggle_btn_) {
      preset_view_toggle_btn_->setIcon(IconHelper::load_symbolic(":/icons/icons/view-list-symbolic.svg", palette(), 16));
      preset_view_toggle_btn_->setToolTip(tr("Switch to List View"));
      preset_view_toggle_btn_->setText("");
    }
  }
  rebuild_preset_dock();
}

void MainWindow::select_preset(std::size_t db_idx) {
  if (db_idx >= preset_database_.presets().size()) return;
  active_preset_index_ = db_idx;
  const auto &p = preset_database_.presets()[db_idx];
  brush::Settings base = canvas_->brush_settings();
  brush::Settings settings = p.brush_settings(base, &preset_database_);
  canvas_->set_brush_settings(settings);

  if (size_spin_) size_spin_->setValue(static_cast<int>(settings.size));
  if (opacity_slider_) opacity_slider_->setValue(static_cast<int>(std::round(settings.opacity * 100.0)));
  set_status(tr("Selected brush preset: %1").arg(QString::fromStdString(p.name)));
}

void MainWindow::on_preset_search_changed(const QString &) {
  rebuild_preset_dock();
}

void MainWindow::on_preset_category_changed(int) {
  rebuild_preset_dock();
}

QTreeWidget *MainWindow::layer_tree() {
  return layer_panel_ ? layer_panel_->tree_widget() : layer_tree_;
}

void MainWindow::rebuild_layer_list() {
  if (layer_panel_) {
    layer_panel_->rebuild_layer_list();
  }
}

void MainWindow::on_toggle_clipping_mask() {
  if (layer_panel_) {
    layer_panel_->on_toggle_clipping_mask();
    update_actions();
  }
}

void MainWindow::on_add_layer() {
  if (layer_panel_) {
    layer_panel_->on_add_layer();
    update_actions();
  }
}

void MainWindow::on_add_group() {
  if (layer_panel_) {
    layer_panel_->on_add_group();
    update_actions();
  }
}

void MainWindow::on_remove_layer() {
  if (layer_panel_) {
    layer_panel_->on_delete_layer();
    update_actions();
  }
}

void MainWindow::on_merge_down() {
  if (layer_panel_) {
    layer_panel_->on_merge_down();
    update_actions();
  }
}

void MainWindow::on_rename_layer() {
  if (layer_panel_) {
    layer_panel_->on_rename_layer();
    update_actions();
  }
}

void MainWindow::on_layer_opacity_changed(int val) {
  if (layer_panel_) {
    layer_panel_->on_opacity_slider_changed(val);
  }
}

void MainWindow::on_layer_blend_changed(int idx) {
  if (layer_panel_) {
    layer_panel_->on_blend_mode_changed(idx);
  }
}

void MainWindow::on_layer_screentone_changed(int idx) {
  if (layer_panel_) {
    layer_panel_->on_screentone_changed(idx);
  }
}

void MainWindow::on_new_document() {
  if (document_.modified()) {
    const auto btn = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("Save changes to current document before creating a new one?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save) on_save_document();
    else if (btn == QMessageBox::Cancel) return;
  }
  document_ = core::Document(1920, 1080);
  if (document_.layer_count() > 0 && document_.layer(0).name() == "1") {
    document_.layer(0).set_name("Layer 1");
  }
  current_path_.clear();
  canvas_->invalidate_gpu_state();
  canvas_->refresh();
  canvas_->zoom_fit();
  rebuild_layer_list();
  update_title();
  update_actions();
}

void MainWindow::on_open_document() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Open Document"), QString(),
      tr("All Supported Files (*.kra *.png);;Krita Documents (*.kra);;PNG Images (*.png)"));
  if (!filename.isEmpty()) {
    open_file(filename.toStdString());
  }
}

void MainWindow::open_file(const std::filesystem::path &path) {
  const std::string ext = path.extension().string();
  bool success = false;
  std::string error;
  if (ext == ".kra") {
    success = io::read_kra(path, document_, error, canvas_->tracker().session_uuid());
  } else {
    QImage img(QString::fromStdString(path.string()));
    if (!img.isNull()) {
      img = img.convertToFormat(QImage::Format_ARGB32);
      document_.reset(img.width(), img.height());
      auto &layer_img = document_.active_layer().image();
      for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
          const QRgb px = line[x];
          layer_img.set_pixel(x, y, core::Rgba8{static_cast<uint8_t>(qRed(px)),
                                                static_cast<uint8_t>(qGreen(px)),
                                                static_cast<uint8_t>(qBlue(px)),
                                                static_cast<uint8_t>(qAlpha(px))});
        }
      }
      document_.active_layer().set_name(path.stem().string());
      success = true;
    }
  }

  if (success) {
    current_path_ = path;
    AppSettings::instance().add_recent_file(QString::fromStdString(path.string()));
    canvas_->timelapse_recorder().init(canvas_->tracker().session_uuid());
    canvas_->invalidate_gpu_state();
    canvas_->refresh();

    // Autosave recovery. Skipped on offscreen (tests) so nothing can block.
    std::filesystem::path auto_path;
    if (QGuiApplication::platformName() != "offscreen" &&
        io::has_autosave(path, auto_path)) {
      const auto choice = QMessageBox::question(
          this, tr("Recover Autosave"),
          tr("An autosaved version of this document is newer than the file on disk.\n"
             "Recover the autosaved changes?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
      if (choice == QMessageBox::Yes) {
        std::string recover_err;
        if (io::read_kra(auto_path, document_, recover_err)) {
          current_path_ = path;
          canvas_->invalidate_gpu_state();
          canvas_->refresh();
        }
      }
      io::remove_autosave(path);
    }

    // Uncommitted WAL journal recovery
    std::filesystem::path wal_path;
    if (QGuiApplication::platformName() != "offscreen" &&
        io::has_uncommitted_wal(path, wal_path)) {
      const auto choice = QMessageBox::question(
          this, tr("Recover Uncommitted Changes"),
          tr("Kreart detected unsaved drawing sessions from an unexpected shutdown.\n"
             "Replay recovered strokes?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
      if (choice == QMessageBox::Yes) {
        std::string rep;
        if (io::recover_document_from_wal(wal_path, document_, rep)) {
          canvas_->invalidate_gpu_state();
          canvas_->refresh();
          set_status(QString::fromStdString(rep));
        }
      }
    }

    canvas_->zoom_fit();
    rebuild_layer_list();
    update_title();
    update_actions();
    set_status(tr("Opened %1").arg(QString::fromStdString(path.filename().string())));
  } else {
    // If file failed to load, check for autosave or WAL journal recovery
    std::filesystem::path auto_path;
    std::filesystem::path wal_path;
    const bool has_auto = io::has_autosave(path, auto_path);
    const bool has_wal = io::has_uncommitted_wal(path, wal_path);

    if (QGuiApplication::platformName() != "offscreen" && (has_auto || has_wal)) {
      const auto choice = QMessageBox::question(
          this, tr("Corrupted Project Recovery"),
          tr("The project file is damaged. A recovery journal or autosave was found.\n"
             "Would you like Kreart to reconstruct the project from the journal?"),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
      if (choice == QMessageBox::Yes) {
        bool rec_ok = false;
        std::string rec_msg;
        if (has_auto) {
          rec_ok = io::read_kra(auto_path, document_, rec_msg);
        }
        if (has_wal) {
          std::string wal_rep;
          if (io::recover_document_from_wal(wal_path, document_, wal_rep)) {
            rec_ok = true;
            rec_msg = wal_rep;
          }
        }
        if (rec_ok) {
          current_path_ = path;
          AppSettings::instance().add_recent_file(QString::fromStdString(path.string()));
          canvas_->invalidate_gpu_state();
          canvas_->refresh();
          canvas_->zoom_fit();
          rebuild_layer_list();
          update_title();
          update_actions();
          set_status(tr("Recovered %1: %2").arg(QString::fromStdString(path.filename().string()), QString::fromStdString(rec_msg)));
          return;
        }
      }
    }
    QMessageBox::warning(this, tr("Open Failed"), tr("Failed to load document: %1\n%2").arg(QString::fromStdString(path.string())).arg(QString::fromStdString(error)));
  }
}

void MainWindow::on_recover_damaged_project() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Recover Damaged Project or Journal"), QString(),
      tr("All Supported Files (*.kra *.wal ~*.kra);;Krita Documents (*.kra);;WAL Journals (*.wal);;All Files (*.*)"));
  if (filename.isEmpty()) return;

  std::filesystem::path p = filename.toStdString();
  std::string report;
  bool recovered = false;

  if (p.extension() == ".wal") {
    core::Document doc(1920, 1080);
    recovered = io::recover_document_from_wal(p, doc, report);
    if (recovered) {
      document_ = std::move(doc);
      current_path_.clear();
    }
  } else {
    std::string kra_err;
    recovered = io::read_kra(p, document_, kra_err);
    std::filesystem::path wal_p;
    if (io::has_uncommitted_wal(p, wal_p)) {
      std::string wal_rep;
      if (io::recover_document_from_wal(wal_p, document_, wal_rep)) {
        report = wal_rep;
        recovered = true;
      }
    } else if (recovered) {
      report = "File loaded successfully. No uncommitted journal entries found.";
    }
  }

  if (recovered) {
    canvas_->invalidate_gpu_state();
    canvas_->refresh();
    canvas_->zoom_fit();
    rebuild_layer_list();
    update_title();
    update_actions();
    QMessageBox::information(this, tr("Project Recovery Summary"),
                             tr("Recovery completed:\n%1").arg(QString::fromStdString(report)));
  } else {
    QMessageBox::warning(this, tr("Recovery Failed"),
                         tr("Could not recover project from %1.\n%2").arg(filename, QString::fromStdString(report)));
  }
}
void MainWindow::on_replay_timelapse() {
  canvas_->timelapse_recorder().flush_pending_frames();
  TimelapseDialog dlg(canvas_->timelapse_recorder(), canvas_->tracker(), this);
  dlg.exec();
}

void MainWindow::on_save_document() {
  if (current_path_.empty()) {
    on_save_as_document();
  } else {
    std::string error;
    document_.mark_composite_dirty();
    document_.composite_image();
    canvas_->timelapse_recorder().flush_pending_frames();
    if (io::write_kra(current_path_, document_, error, canvas_->tracker().session_uuid())) {
      document_.mark_saved();
      canvas_->wal_journal().checkpoint();
      io::remove_autosave(current_path_);
      set_status(tr("Saved to %1").arg(QString::fromStdString(current_path_.string())));

      std::filesystem::path thumb_path = current_path_.parent_path() / (current_path_.stem().string() + ".thumb.png");
      std::string thumb_err;
      io::write_png(thumb_path, document_.composite_with_floating(), thumb_err);
    } else {
      QMessageBox::critical(this, tr("Save Failed"), tr("Could not save to %1:\n%2").arg(QString::fromStdString(current_path_.string())).arg(QString::fromStdString(error)));
    }
  }
}

void MainWindow::on_save_as_document() {
  const QString filename = QFileDialog::getSaveFileName(
      this, tr("Save Document As"), QString(), tr("Krita Documents (*.kra)"));
  if (!filename.isEmpty()) {
    current_path_ = filename.toStdString();
    if (current_path_.extension() != ".kra") current_path_ += ".kra";
    on_save_document();
  }
}

void MainWindow::on_export_png() {
  const QString filename = QFileDialog::getSaveFileName(
      this, tr("Export PNG"), QString(), tr("PNG Images (*.png)"));
  if (!filename.isEmpty()) {
    std::filesystem::path p = filename.toStdString();
    if (p.extension() != ".png") p += ".png";
    std::string error;
    if (io::write_png(p, document_.composite_with_floating(), error)) {
      set_status(tr("Exported PNG: %1").arg(QString::fromStdString(p.filename().string())));
    } else {
      QMessageBox::critical(this, tr("Export Failed"), tr("Failed to export PNG: %1").arg(QString::fromStdString(error)));
    }
  }
}

bool MainWindow::import_image_as_layer(const std::filesystem::path &path) {
  if (layer_panel_) {
    bool ok = layer_panel_->import_image(path);
    if (ok) {
      update_title();
      update_actions();
      set_status(tr("Imported layer '%1'").arg(QString::fromStdString(path.stem().string())));
    }
    return ok;
  }
  return false;
}

void MainWindow::on_import_image_as_layer() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Import Image as Layer"), QString(), tr("PNG Images (*.png)"));
  if (!filename.isEmpty()) {
    import_image_as_layer(filename.toStdString());
  }
}

bool MainWindow::import_brush_pack(const std::filesystem::path &path) {
  const auto user_dir = resources::ResourceDatabase::user_resource_directory();
  std::error_code ec;
  if (path.extension() == ".bundle") {
    const auto dest = user_dir / "bundles";
    std::filesystem::create_directories(dest, ec);
    std::filesystem::copy_file(path, dest / path.filename(), std::filesystem::copy_options::overwrite_existing, ec);
  } else {
    const auto dest = user_dir / "presets";
    std::filesystem::create_directories(dest, ec);
    std::filesystem::copy_file(path, dest / path.filename(), std::filesystem::copy_options::overwrite_existing, ec);
  }
  load_presets();
  return true;
}

void MainWindow::on_import_brush_pack() {
  const QString filename = QFileDialog::getOpenFileName(
      this, tr("Import Brush Pack"), QString(), tr("Brush Bundles and Presets (*.bundle *.kpp)"));
  if (!filename.isEmpty()) {
    import_brush_pack(filename.toStdString());
  }
}

void MainWindow::on_undo() {
  document_.undo();
  canvas_->refresh();
  update_title();
  update_actions();
}

void MainWindow::on_redo() {
  document_.redo();
  canvas_->refresh();
  update_title();
  update_actions();
}

void MainWindow::sync_system_clipboard_to_doc() {
  const QClipboard *sys_clip = QGuiApplication::clipboard();
  if (sys_clip && sys_clip->mimeData() && sys_clip->mimeData()->hasImage()) {
    QImage img = sys_clip->image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!img.isNull() && img.width() > 0 && img.height() > 0) {
      std::vector<core::Rgba8> pixels(static_cast<std::size_t>(img.width()) * img.height());
      std::vector<std::uint8_t> mask(static_cast<std::size_t>(img.width()) * img.height(), 255);
      for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
          const QColor c = img.pixelColor(x, y);
          pixels[static_cast<std::size_t>(y) * img.width() + x] = core::Rgba8{
            static_cast<std::uint8_t>(c.red()),
            static_cast<std::uint8_t>(c.green()),
            static_cast<std::uint8_t>(c.blue()),
            static_cast<std::uint8_t>(c.alpha())
          };
        }
      }
      document_.set_clipboard(img.width(), img.height(), std::move(pixels), std::move(mask));
      system_clipboard_newer_ = false;
    }
  }
}

void MainWindow::on_copy() {
  document_.copy_selection();
  const auto &cb = document_.clipboard();
  if (cb.valid && cb.width > 0 && cb.height > 0) {
    QImage qimg(cb.width, cb.height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < cb.height; ++y) {
      for (int x = 0; x < cb.width; ++x) {
        const std::size_t idx = static_cast<std::size_t>(y) * cb.width + x;
        const auto &px = cb.pixels[idx];
        const std::uint8_t m = (!cb.mask.empty()) ? cb.mask[idx] : 255;
        const std::uint8_t a = static_cast<std::uint8_t>((static_cast<int>(px.a) * m) / 255);
        qimg.setPixelColor(x, y, QColor(px.r, px.g, px.b, a));
      }
    }
    system_clipboard_newer_ = false;
    QGuiApplication::clipboard()->setImage(qimg);
  }
}

void MainWindow::on_cut() {
  on_copy();
  document_.cut_selection();
  canvas_->refresh();
}

void MainWindow::on_paste() {
  if (system_clipboard_newer_ || !document_.clipboard().valid) {
    sync_system_clipboard_to_doc();
  }
  if (document_.paste_selection()) {
    canvas_->refresh();
  }
}

void MainWindow::on_paste_as_new_layer() {
  if (system_clipboard_newer_ || !document_.clipboard().valid) {
    sync_system_clipboard_to_doc();
  }
  if (document_.paste_as_new_layer()) {
    rebuild_layer_list();
    canvas_->refresh();
  }
}

void MainWindow::on_deselect() {
  if (document_.has_floating_selection()) {
    document_.commit_move_selection();
  }
  document_.begin_change();
  document_.selection().clear();
  document_.commit_change();
  canvas_->refresh();
}

void MainWindow::on_invert_selection() {
  document_.begin_change();
  document_.selection().invert();
  document_.commit_change();
  canvas_->refresh();
}

void MainWindow::on_select_all() {
  document_.begin_change();
  document_.selection().set_rect(0, 0, document_.width(), document_.height());
  document_.commit_change();
  canvas_->refresh();
}

void MainWindow::on_clear_selection() {
  if (document_.has_floating_selection()) {
    document_.cancel_move_selection();
  }
  document_.clear_selection_content();
  document_.begin_change();
  document_.selection().clear();
  document_.commit_change();
  canvas_->refresh();
}

void MainWindow::on_feather_selection() {
  bool ok = false;
  double r = QInputDialog::getDouble(this, tr("Feather Selection"), tr("Feather radius (px):"), 4.0, 0.1, 100.0, 1, &ok);
  if (ok) {
    document_.feather_selection(r);
    canvas_->refresh();
  }
}

void MainWindow::on_grow_selection() {
  bool ok = false;
  int px = QInputDialog::getInt(this, tr("Grow Selection"), tr("Grow by (px):"), 5, 1, 100, 1, &ok);
  if (ok) {
    document_.grow_selection(px);
    canvas_->refresh();
  }
}

void MainWindow::on_shrink_selection() {
  bool ok = false;
  int px = QInputDialog::getInt(this, tr("Shrink Selection"), tr("Shrink by (px):"), 5, 1, 100, 1, &ok);
  if (ok) {
    document_.shrink_selection(px);
    canvas_->refresh();
  }
}

void MainWindow::on_border_selection() {
  bool ok = false;
  int px = QInputDialog::getInt(this, tr("Border Selection"), tr("Border width (px):"), 5, 1, 100, 1, &ok);
  if (ok) {
    document_.border_selection(px);
    canvas_->refresh();
  }
}

void MainWindow::on_zoom_in() { canvas_->zoom_in(); }
void MainWindow::on_zoom_out() { canvas_->zoom_out(); }
void MainWindow::on_zoom_fit() { canvas_->zoom_fit(); }
void MainWindow::on_rotate_left() { canvas_->rotate_by(-15.0 * std::numbers::pi / 180.0); }
void MainWindow::on_rotate_right() { canvas_->rotate_by(15.0 * std::numbers::pi / 180.0); }
void MainWindow::on_reset_rotation() { canvas_->reset_rotation(); }
void MainWindow::on_mirror_horizontal() { canvas_->toggle_mirror_horizontal(); }
void MainWindow::on_mirror_vertical() { canvas_->toggle_mirror_vertical(); }
void MainWindow::on_pixelated_toggled(bool checked) { canvas_->set_pixelated(checked); }
void MainWindow::on_grid_toggled(bool checked) { canvas_->set_show_grid(checked); }

void MainWindow::on_canvas_size() {
  CanvasSizeDialog dlg(this, document_, canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    update_title();
    update_actions();
  }
}

void MainWindow::on_scale_image() {
  ScaleImageDialog dlg(this, document_, canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    update_title();
    update_actions();
  }
}

void MainWindow::on_filter_hsv() {
  HsvFilterDialog dlg(this, document_, *canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    update_title();
    update_actions();
  }
}

void MainWindow::on_filter_brightness_contrast() {
  BrightnessContrastDialog dlg(this, document_, *canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    update_title();
    update_actions();
  }
}

void MainWindow::on_filter_color_balance() {
  ColorBalanceDialog dlg(this, document_, *canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    update_title();
    update_actions();
  }
}

void MainWindow::on_filter_gaussian_blur() {
  GaussianBlurDialog dlg(this, document_, *canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    update_title();
    update_actions();
  }
}

void MainWindow::on_preferences() {
  PreferencesDialog dlg(this);
  connect(&dlg, &PreferencesDialog::installedBrushesChanged, this, &MainWindow::load_presets);
  dlg.exec();
}

void MainWindow::create_new_document(int width, int height, const QString &title) {
  document_.reset(width, height);
  const auto dir = GalleryView::projects_directory();
  current_path_ = dir / (title.toStdString() + ".kra");
  canvas_->reposition_symmetry_axes();
  canvas_->invalidate_gpu_state();
  canvas_->refresh();
  canvas_->zoom_fit();
  update_title();
  update_actions();
  rebuild_layer_list();

  std::string err;
  io::write_kra(current_path_, document_, err);
  std::filesystem::path thumb_path = current_path_.parent_path() / (current_path_.stem().string() + ".thumb.png");
  io::write_png(thumb_path, document_.composite_with_floating(), err);
}

void MainWindow::show_initial_gallery() {
  if (!gallery_window_) {
    gallery_window_ = new ProjectGalleryWindow(this);
  }
  gallery_window_->refresh();
  gallery_window_->show();
}

void MainWindow::on_show_gallery() {
  if (!gallery_window_) {
    gallery_window_ = new ProjectGalleryWindow(this);
  }
  gallery_window_->refresh();
  gallery_window_->show();
  gallery_window_->raise();
  gallery_window_->activateWindow();
}

void MainWindow::on_grid_settings() {
  GridSetupDialog dlg(this, canvas_);
  if (dlg.exec() == QDialog::Accepted) {
    if (grid_act_) {
      grid_act_->setChecked(canvas_->show_grid());
    }
  }
}

} // namespace kreart::ui
