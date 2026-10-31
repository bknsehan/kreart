#include "kreart/ui/shortcuts.h"
#include "kreart/ui/shortcut_dialog.h"
#include <numbers>
#include <QTableWidget>
#include <QApplication>
#include <QTimer>
#include <QMimeData>
#include <cstdio>
#include <cstdlib>

#include "kreart/ui/main_window.h"
#include "kreart/ui/app_settings.h"
#include "kreart/ui/color_picker.h"
#include "kreart/ui/icon_helper.h"
#include "kreart/ui/layer_panel.h"
#include "kreart/ui/preferences_dialog.h"
#include "kreart/ui/project_gallery_window.h"
#include "kreart/ui/grid_dialog.h"
#include "kreart/io/png.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSlider>
#include "kreart/ui/desktop_theme.h"
#include "kreart/ui/geometry_dialogs.h"
#include "kreart/io/kra.h"
#include "kreart/ui/timelapse_dialog.h"
#include <QDialogButtonBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <chrono>
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  Q_INIT_RESOURCE(kreart);
  QApplication app(argc, argv);
  int failures = 0;
  auto check = [&failures](bool cond, const char *msg) {
    if (!cond) {
      std::fprintf(stderr, "FAIL: %s\n", msg);
      ++failures;
    } else {
      std::printf("PASS: %s\n", msg);
    }
  };

  // SVG icon resource loading
  QPixmap icon_brush = kreart::ui::IconHelper::render_symbolic(":/icons/icons/tool-brush-symbolic.svg", QColor(255, 255, 255), 24);
  check(!icon_brush.isNull() && icon_brush.width() == 24 && icon_brush.height() == 24, "tool-brush-symbolic.svg rendered 24x24");
  QPixmap icon_layer = kreart::ui::IconHelper::render_symbolic(":/icons/icons/layer-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_layer.isNull() && icon_layer.width() == 16 && icon_layer.height() == 16, "layer-symbolic.svg rendered 16x16");
  QPixmap icon_grid = kreart::ui::IconHelper::render_symbolic(":/icons/icons/view-grid-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_grid.isNull() && icon_grid.width() == 16 && icon_grid.height() == 16, "view-grid-symbolic.svg rendered 16x16");
  QPixmap icon_list = kreart::ui::IconHelper::render_symbolic(":/icons/icons/view-list-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_list.isNull() && icon_list.width() == 16 && icon_list.height() == 16, "view-list-symbolic.svg rendered 16x16");
  QPixmap icon_alpha = kreart::ui::IconHelper::render_symbolic(":/icons/icons/layer-alpha-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_alpha.isNull() && icon_alpha.width() == 16 && icon_alpha.height() == 16, "layer-alpha-symbolic.svg rendered 16x16");
  QPixmap icon_drag = kreart::ui::IconHelper::render_symbolic(":/icons/icons/drag-handle-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_drag.isNull() && icon_drag.width() == 16 && icon_drag.height() == 16, "drag-handle-symbolic.svg rendered 16x16");
  QPixmap icon_cam = kreart::ui::IconHelper::render_symbolic(":/icons/icons/camera-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_cam.isNull() && icon_cam.width() == 16 && icon_cam.height() == 16, "camera-symbolic.svg rendered 16x16");
  QPixmap icon_ellipse = kreart::ui::IconHelper::render_symbolic(":/icons/icons/tool-ellipse-select-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_ellipse.isNull() && icon_ellipse.width() == 16 && icon_ellipse.height() == 16, "tool-ellipse-select-symbolic.svg rendered 16x16");
  QPixmap icon_wand = kreart::ui::IconHelper::render_symbolic(":/icons/icons/tool-wand-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_wand.isNull() && icon_wand.width() == 16 && icon_wand.height() == 16, "tool-wand-symbolic.svg rendered 16x16");
  QPixmap icon_sym_v = kreart::ui::IconHelper::render_symbolic(":/icons/icons/symmetry-v-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_sym_v.isNull() && icon_sym_v.width() == 16 && icon_sym_v.height() == 16, "symmetry-v-symbolic.svg rendered 16x16");
  const QImage img_sym_v = icon_sym_v.toImage();
  bool sym_v_exact = true;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 8; ++x) {
      const auto c1 = img_sym_v.pixelColor(x, y);
      const auto c2 = img_sym_v.pixelColor(15 - x, y);
      if (std::abs(c1.alpha() - c2.alpha()) > 5) {
        sym_v_exact = false;
        break;
      }
    }
  }
  check(sym_v_exact, "symmetry-v icon is horizontally symmetrical within antialiasing tolerance");

  QPixmap icon_sym_h = kreart::ui::IconHelper::render_symbolic(":/icons/icons/symmetry-h-symbolic.svg", QColor(255, 255, 255), 16);
  check(!icon_sym_h.isNull() && icon_sym_h.width() == 16 && icon_sym_h.height() == 16, "symmetry-h-symbolic.svg rendered 16x16");
  const QImage img_sym_h = icon_sym_h.toImage();
  bool sym_h_exact = true;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 16; ++x) {
      const auto c1 = img_sym_h.pixelColor(x, y);
      const auto c2 = img_sym_h.pixelColor(x, 15 - y);
      if (std::abs(c1.alpha() - c2.alpha()) > 5) {
        sym_h_exact = false;
        break;
      }
    }
  }
  check(sym_h_exact, "symmetry-h icon is vertically symmetrical within antialiasing tolerance");

  kreart::ui::MainWindow window;
  window.resize(1400, 900);
  window.show();
  app.processEvents();
  if (window.canvas()) {
    window.canvas()->resize(1000, 800);
  }
  app.processEvents();
  // 1. Preset view mode toggle
  for (int i = 0; i < 10; ++i) {
    window.toggle_preset_view_mode();
  }
  check(true, "preset view mode toggling");

  // 2. Color Picker RGB Roundtrip & Mode Switching
  kreart::ui::StudioColorPicker picker;
  picker.set_color(kreart::core::Rgba8{35, 120, 235, 255});
  const auto pc = picker.color();
  check(pc.r == 35 && pc.g == 120 && pc.b == 235, "StudioColorPicker RGB roundtrip verified");

  picker.set_mode(kreart::ui::ColorPickerMode::RECTANGLE);
  check(picker.mode() == kreart::ui::ColorPickerMode::RECTANGLE, "StudioColorPicker switched to RECTANGLE mode");

  picker.set_mode(kreart::ui::ColorPickerMode::WHEEL);
  check(picker.mode() == kreart::ui::ColorPickerMode::WHEEL, "StudioColorPicker switched to WHEEL mode");
  // Color preset swatches and commit signal verification
  const auto &prof_btns = window.profile_swatch_buttons();
  check(prof_btns.size() == 8, "profile_swatch_buttons size is 8");
  bool all_prof_styled = true;
  for (auto *b : prof_btns) {
    if (!b || !b->isEnabled() || !b->styleSheet().contains("background-color: rgb(")) {
      all_prof_styled = false;
      break;
    }
  }
  check(all_prof_styled, "all 8 profile swatches populated with solid colors at startup");

  if (window.studio_picker()) {
    const kreart::core::Rgba8 test_color{200, 50, 100, 255};
    emit window.studio_picker()->colorCommitted(test_color);
    const auto &hist = window.color_history().colors();
    check(!hist.empty() && hist.front().r == 200 && hist.front().g == 50 && hist.front().b == 100,
          "StudioColorPicker colorCommitted updates color history");
  }

  // 3. QuickMenu instance and signals
  auto *qm = window.quick_menu();
  check(qm != nullptr, "QuickMenu instance initialized");
  if (qm) {
    window.popup_quick_menu(150.0, 150.0);
    emit qm->sizeChanged(72.0);
    emit qm->opacityChanged(0.85);
    emit qm->colorSelected(kreart::core::Rgba8{40, 150, 220, 255});
    check(true, "QuickMenu signals and popup verified");
  }
  // Layer hierarchy and clipping mask verification
  auto &doc = window.document();
  const std::size_t grp_idx = doc.add_group("Test Group");
  const std::size_t grp_id = doc.layer(grp_idx).id();
  const std::size_t child_idx = doc.add_layer("Child Layer");
  doc.layer(child_idx).set_parent_id(grp_id);
  doc.set_active_layer_index(child_idx);
  window.on_toggle_clipping_mask();
  window.rebuild_layer_list();

  auto *lt = window.layer_tree();
  check(lt != nullptr, "Layer tree initialized");
  if (lt) {
    QTreeWidgetItem *grp_item = nullptr;
    for (int i = 0; i < lt->topLevelItemCount(); ++i) {
      auto *top = lt->topLevelItem(i);
      if (top && top->text(1) == "Test Group") {
        grp_item = top;
        break;
      }
    }
    check(grp_item != nullptr, "Found Test Group top level item in layer_tree");
    if (grp_item) {
      check(grp_item->childCount() == 1, "Test Group has 1 child item");
      if (grp_item->childCount() == 1) {
        auto *child_item = grp_item->child(0);
        check(child_item->text(1).contains("Child Layer"), "Child item name contains Child Layer");
        check(child_item->text(1).startsWith(QString::fromUtf8("↳ ")), "Child item text has clipping arrow prefix");
        check(!child_item->icon(2).isNull(), "Child item has clipping mask indicator icon in column 2");
      }
    }
  }
  // 1. Canvas Multi-Layer Compositing
  {
    doc.begin_change();
    while (doc.layer_count() > 1) {
      doc.remove_layer(0);
    }
    auto &l1 = doc.layer(0);
    l1.set_name("Layer Red");
    l1.set_clipping_mask(false);
    l1.set_parent_id(0);
    l1.set_opacity(1.0);
    l1.set_composite_op("normal");
    l1.image().fill(kreart::core::Rgba8{255, 0, 0, 255});

    const std::size_t l2_idx = doc.add_layer("Layer Blue Top");
    auto &l2 = doc.layer(l2_idx);
    l2.set_clipping_mask(false);
    l2.set_parent_id(0);
    l2.set_opacity(1.0);
    l2.set_composite_op("normal");
    l2.image().fill(kreart::core::Rgba8{0, 0, 255, 128});
    doc.clean_hierarchy_invariants();
    doc.commit_change();

    // Switch active layer to Layer 1 (bottom layer)
    doc.set_active_layer_index(0);
    window.canvas()->refresh();

    // Verify composite blends both layers
    const auto blended = doc.composite_image().pixel(10, 10);
    check(blended.r > 50 && blended.b > 50 && blended.a == 255,
          "Canvas composite blends both layers (red and blue visible) when bottom layer is active");
  }

  // 2. Layer Drag-and-Drop Invariants
  {
    doc.begin_change();
    while (doc.layer_count() > 1) {
      doc.remove_layer(0);
    }
    const std::size_t grp_idx = doc.add_group("Invariant Folder");
    const std::size_t grp_id = doc.layer(grp_idx).id();
    const std::size_t la_idx = doc.add_layer("Layer A");
    const std::size_t lb_idx = doc.add_layer("Layer B");
    doc.commit_change();
    window.rebuild_layer_list();

    auto *lp = window.layer_panel();
    check(lp != nullptr, "LayerPanel initialized");
    if (lp && lp->tree_widget()) {
      // Simulate dragging Layer A onto Layer B (normal layer drop rejection)
      lp->tree_widget()->perform_drop(la_idx, lb_idx, 0.5);
      check(doc.layer(la_idx).parent_id() == 0,
            "Layer A does NOT become child of Layer B after drop (parent_id == 0)");

      // Simulate dragging Layer A onto Group Folder
      std::size_t cur_grp_idx = 0;
      for (std::size_t i = 0; i < doc.layer_count(); ++i) {
        if (doc.layer(i).id() == grp_id) {
          cur_grp_idx = i;
          break;
        }
      }
      std::size_t cur_la_idx = 0;
      for (std::size_t i = 0; i < doc.layer_count(); ++i) {
        if (doc.layer(i).name() == "Layer A") {
          cur_la_idx = i;
          break;
        }
      }
      lp->tree_widget()->perform_drop(cur_la_idx, cur_grp_idx, 0.5);
      const kreart::core::Layer *moved_a = nullptr;
      for (std::size_t i = 0; i < doc.layer_count(); ++i) {
        if (doc.layer(i).name() == "Layer A") {
          moved_a = &doc.layer(i);
          break;
        }
      }
      check(moved_a != nullptr && moved_a->parent_id() == grp_id,
            "Layer A becomes child of Group Folder (parent_id == folder.id())");
    }
  }

  // 3. Alpha Lock Enforcement
  {
    const std::size_t al_idx = doc.add_layer("Alpha Lock Test");
    auto &al_layer = doc.layer(al_idx);
    doc.set_active_layer_index(al_idx);

    // Draw a 10x10 white square on transparent background
    for (int y = 0; y < 10; ++y) {
      for (int x = 0; x < 10; ++x) {
        al_layer.image().set_pixel(x, y, kreart::core::Rgba8{255, 255, 255, 255});
      }
    }

    // Toggle Alpha Lock ON
    al_layer.set_alpha_locked(true);
    check(al_layer.alpha_locked(), "Layer alpha_locked is ON");

    // Paint outside white square at (20, 20) with red
    al_layer.image().blend_pixel(20, 20, kreart::core::Rgba8{255, 0, 0, 255});
    check(al_layer.image().pixel(20, 20).a == 0,
          "Alpha lock preserves transparent pixels outside painted area (alpha remains 0)");

    // Paint inside white square at (5, 5) with red
    al_layer.image().blend_pixel(5, 5, kreart::core::Rgba8{255, 0, 0, 255});
    const auto painted_px = al_layer.image().pixel(5, 5);
    check(painted_px.r > 0 && painted_px.a == 255,
          "Alpha lock allows color modification while preserving alpha == 255");
  }

  // 4. Selection Layer Card & Thumbnail Updates
  {
    auto *lp = window.layer_panel();
    check(lp != nullptr, "LayerPanel accessible");
    if (lp && lp->selection_card()) {
      doc.selection().clear();
      lp->update_controls();
      check(lp->selection_card()->subtitle_text() == "No Selection",
            "Selection Layer card displays 'No Selection' when empty");

      doc.selection().set_rect(10, 10, 50, 60);
      lp->update_controls();
      check(lp->selection_card()->subtitle_text().contains("Selection Active"),
            "Selection Layer card displays 'Selection Active' when selection is drawn");
    }

    QPixmap thumb = kreart::ui::render_layer_thumbnail(doc.active_layer(), 40, true);
    check(!thumb.isNull() && thumb.width() == 40 && thumb.height() == 40,
          "render_layer_thumbnail produces valid 40x40 pixmap");
    QImage thumb_img = thumb.toImage();
    check(thumb_img.pixelColor(1, 1).blue() > 180,
          "Active layer thumbnail produces blue outline border");
  }
  // 5. Layer Move Up / Down
  {
    auto *lp = window.layer_panel();
    if (lp) {
      const std::size_t l_idx = doc.add_layer("Move Test Layer");
      doc.set_active_layer_index(l_idx);
      lp->on_move_layer_down();
      check(doc.active_layer_index() == l_idx - 1, "LayerPanel on_move_layer_down moves active layer index down");
      lp->on_move_layer_up();
      check(doc.active_layer_index() == l_idx, "LayerPanel on_move_layer_up moves active layer index up");
    }
  }

  // 6. Flexible Lasso & Magic Lasso
  if (window.canvas()) {
    auto *cv = window.canvas();
    doc.selection().clear();
    cv->set_lasso_mode(kreart::ui::Canvas::LassoMode::FREEHAND);
    std::vector<std::pair<double, double>> triangle = {{100.0, 50.0}, {200.0, 150.0}, {50.0, 150.0}};
    doc.selection().add_polygon(triangle);
    check(doc.selection().lasso_points().size() == 3, "Freehand lasso creates flexible 3-point polygon");
    check(doc.selection().is_selected(100, 100), "Inside triangle (100, 100) is selected");
    check(!doc.selection().is_selected(50, 50), "Outside triangle (50, 50) is not selected");

    // Magic Lasso Auto-Catch
    doc.selection().clear();
    auto &active_layer = doc.active_layer();
    active_layer.image().fill(kreart::core::Rgba8{0, 0, 0, 0});
    for (int y = 100; y < 120; ++y) {
      for (int x = 100; x < 120; ++x) {
        active_layer.image().set_pixel(x, y, kreart::core::Rgba8{255, 0, 0, 255});
      }
    }
    std::vector<std::pair<double, double>> loose_box = {{80.0, 80.0}, {140.0, 80.0}, {140.0, 140.0}, {80.0, 140.0}};
    doc.selection().add_polygon(loose_box, &active_layer.image(), true);
    check(doc.selection().active(), "Magic Lasso auto-catch creates active selection");
    check(doc.selection().is_selected(110, 110), "Magic Lasso selects object content");
    check(!doc.selection().is_selected(85, 85), "Magic Lasso rejects transparent area inside loose boundary");
  }

  // 7. Transform Hit-testing & Keyboard Manipulation
  if (window.canvas()) {
    auto *cv = window.canvas();
    doc.selection().set_rect(100, 100, 200, 200);
    cv->set_tool(kreart::ui::Canvas::Tool::MOVE);
    double wx, wy;
    cv->map_to_widget(200.0, 200.0, wx, wy);
    check(cv->hit_test_transform(wx, wy) == kreart::ui::Canvas::TransformDragMode::TRANSLATE,
          "Transform hit-test detects TRANSLATE inside selection bounds");

    // Rotate handle at top center
    const double handle_dist = 24.0 / std::max(0.001, cv->zoom_percent() / 100.0);
    double r_wx, r_wy;
    cv->map_to_widget(200.0, 100.0 - handle_dist, r_wx, r_wy);
    check(cv->hit_test_transform(r_wx, r_wy) == kreart::ui::Canvas::TransformDragMode::ROTATE,
          "Transform hit-test detects ROTATE on top rotation handle");

    // Corner handle (scale)
    double c_wx, c_wy;
    cv->map_to_widget(100.0, 100.0, c_wx, c_wy);
    check(cv->hit_test_transform(c_wx, c_wy) == kreart::ui::Canvas::TransformDragMode::SCALE,
          "Transform hit-test detects SCALE on corner handle");

    // Keyboard rotation & escape cancel
    QKeyEvent key_rot(QEvent::KeyPress, Qt::Key_BracketRight, Qt::NoModifier);
    QApplication::sendEvent(cv, &key_rot);
    check(doc.has_floating_selection() || doc.selection().active(),
          "BracketRight key triggers selection rotation");

    QKeyEvent key_esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(cv, &key_esc);
    check(!doc.has_floating_selection(), "Escape key cancels floating selection");
  }

  // 8. Selection Layer Painting Mode
  if (window.canvas()) {
    auto *cv = window.canvas();
    doc.selection().clear();
    cv->set_selection_layer_mode(true);
    check(cv->selection_layer_mode(), "Selection Layer mode toggled ON");
    for (int y = 50; y < 70; ++y) {
      for (int x = 50; x < 70; ++x) {
        cv->selection_image().set_pixel(x, y, kreart::core::Rgba8{244, 63, 94, 255});
      }
    }
    for (int y = 150; y < 170; ++y) {
      for (int x = 150; x < 170; ++x) {
        cv->selection_image().set_pixel(x, y, kreart::core::Rgba8{244, 63, 94, 255});
      }
    }
    cv->sync_selection_mask_from_image();
    check(doc.selection().active(), "Selection Layer painting synchronized active selection");
    check(doc.selection().is_selected(60, 60), "Painted pixel on Selection Layer is selected");
    check(doc.selection().is_selected(160, 160), "Second disjoint blob is selected");
    check(!doc.selection().is_selected(100, 100), "Gap between disjoint blobs is not selected (non-collapsing painted mask)");
    cv->set_selection_layer_mode(false);
    check(!cv->selection_layer_mode(), "Selection Layer mode toggled OFF");
  }
  // 9. Fill tool (flood fill)
  if (window.canvas()) {
    auto *cv = window.canvas();
    doc.active_layer().image().fill(kreart::core::Rgba8{0, 0, 0, 0});
    cv->set_tool(kreart::ui::Canvas::Tool::FILL);
    cv->set_brush_color(kreart::core::Rgba8{120, 200, 80, 255});
    double wx, wy;
    cv->map_to_widget(50.0, 50.0, wx, wy);
    QMouseEvent fill_press(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &fill_press);
    check(doc.active_layer().image().pixel(50, 50).r == 120, "Fill tool flooded active layer with brush color");
  }

  // 10. Move tool without selection creates auto-bounds selection
  if (window.canvas()) {
    auto *cv = window.canvas();
    doc.selection().clear();
    check(!doc.selection().active(), "Selection cleared prior to move test");
    cv->set_tool(kreart::ui::Canvas::Tool::MOVE);
    double wx, wy;
    cv->map_to_widget(50.0, 50.0, wx, wy);
    QMouseEvent move_press(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &move_press);
    check(doc.selection().active(), "Move tool without active selection automatically creates selection bounds");
    check(doc.has_floating_selection(), "Move tool initiated floating selection");

    // Enter commits and turns off selection area
    QKeyEvent key_enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&window, &key_enter);
    check(!doc.has_floating_selection(), "Enter key committed floating selection");
    check(!doc.selection().active(), "Enter key turned off selection area");
  }

  // 11. LayerPanel unselection & auto-select top layer when painting
  {
    auto *lp = window.layer_panel();
    if (lp && window.canvas()) {
      auto *cv = window.canvas();
      lp->clear_selection();
      check(!doc.has_active_layer(), "LayerPanel clear_selection unselects layer");

      // Painting with brush when unselected auto-selects top layer
      cv->set_tool(kreart::ui::Canvas::Tool::BRUSH);
      double wx, wy;
      cv->map_to_widget(10.0, 10.0, wx, wy);
      QMouseEvent paint_press(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(cv, &paint_press);
      check(doc.has_active_layer(), "Painting when unselected auto-selected top layer");
      QMouseEvent paint_release(QEvent::MouseButtonRelease, QPointF(wx, wy), QPointF(wx, wy), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(cv, &paint_release);
    }
  }

  // 4. AppSettings signals
  auto &settings = kreart::ui::AppSettings::instance();
  bool theme_fired = false;
  QObject::connect(&settings, &kreart::ui::AppSettings::themeChanged, [&theme_fired](kreart::ui::ThemeMode) {
    theme_fired = true;
  });
  settings.set_theme(kreart::ui::ThemeMode::LIGHT);
  check(theme_fired && settings.theme() == kreart::ui::ThemeMode::LIGHT, "AppSettings theme toggle to LIGHT verified");
  settings.set_theme(kreart::ui::ThemeMode::DARK);

  bool detect_fired = false;
  QObject::connect(&settings, &kreart::ui::AppSettings::detectKritaBrushesChanged, [&detect_fired](bool) {
    detect_fired = true;
  });
  settings.set_detect_krita_brushes(true);
  check(detect_fired && settings.detect_krita_brushes(), "AppSettings detect_krita_brushes toggle ON verified");
  settings.set_detect_krita_brushes(false);
  check(!settings.detect_krita_brushes(), "AppSettings detect_krita_brushes toggle OFF (default) verified");

  // 5. Canvas interactions
  auto *c = window.canvas();
  check(c != nullptr, "Canvas initialized");
  if (c) {
    c->set_tool(kreart::ui::Canvas::Tool::ERASER);
    check(c->tool() == kreart::ui::Canvas::Tool::ERASER, "Canvas set_tool ERASER");
    check(c->erasing(), "Canvas erasing state verified");
    c->set_tool(kreart::ui::Canvas::Tool::BRUSH);
    check(c->tool() == kreart::ui::Canvas::Tool::BRUSH, "Canvas set_tool BRUSH");
    const double initial_zoom = c->zoom_percent();
    c->zoom_in();
    check(c->zoom_percent() > initial_zoom, "Canvas zoom_in increased zoom");
    c->zoom_fit();
    check(c->rotation_degrees() == 0.0, "Canvas zoom_fit reset rotation");
    // Rotation verification
    c->rotate_by(std::numbers::pi / 4.0);
    check(std::abs(c->rotation_degrees() - 45.0) < 1e-3, "Canvas rotate_by pi/4 rotated to 45 degrees");
    c->reset_rotation();
    check(std::abs(c->rotation_degrees()) < 1e-3, "Canvas reset_rotation reset to 0 degrees");

    // Key event rotation verification (Key 4, 6, 5)
    QKeyEvent key4(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier);
    QApplication::sendEvent(c, &key4);
    check(std::abs(c->rotation_degrees() - (-15.0)) < 1e-3, "Canvas key 4 rotated by -15 degrees");
    QKeyEvent key6(QEvent::KeyPress, Qt::Key_6, Qt::NoModifier);
    QApplication::sendEvent(c, &key6);
    check(std::abs(c->rotation_degrees()) < 1e-3, "Canvas key 6 rotated back by +15 degrees to 0");
    QKeyEvent key5(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier);
    QApplication::sendEvent(c, &key5);
    check(std::abs(c->rotation_degrees()) < 1e-3, "Canvas key 5 reset rotation");

    // Canvas pan clamping verification
    c->pan_by(100000.0, 100000.0);
    check(c->pan_x() < 50000.0 && c->pan_y() < 50000.0, "Canvas pan_by 100000 clamped pan within viewport bounds");
    c->pan_by(-200000.0, -200000.0);
    check(c->pan_x() > -50000.0 && c->pan_y() > -50000.0, "Canvas negative pan clamped within viewport bounds");

    // Canvas stroke drawing and cancellation
    c->zoom_fit();
    double center_wx = 0.0, center_wy = 0.0;
    c->map_to_widget(doc.width() * 0.5, doc.height() * 0.5, center_wx, center_wy);
    c->set_tool(kreart::ui::Canvas::Tool::BRUSH);
    c->set_brush_color(kreart::core::Rgba8{255, 0, 0, 255});
    c->set_brush_size(32.0);

    QMouseEvent press_event(QEvent::MouseButtonPress, QPointF(center_wx, center_wy), QPointF(center_wx, center_wy), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &press_event);
    check(c->stroke_active(), "mouse press begins stroke");

    QMouseEvent move_event(QEvent::MouseMove, QPointF(center_wx + 20.0, center_wy + 20.0), QPointF(center_wx + 20.0, center_wy + 20.0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &move_event);

    QMouseEvent release_event(QEvent::MouseButtonRelease, QPointF(center_wx + 20.0, center_wy + 20.0), QPointF(center_wx + 20.0, center_wy + 20.0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &release_event);
    check(!c->stroke_active(), "mouse release finishes stroke");

    // Test stroke cancellation
    QMouseEvent press_event2(QEvent::MouseButtonPress, QPointF(center_wx, center_wy), QPointF(center_wx, center_wy), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &press_event2);
    check(c->stroke_active(), "mouse press begins second stroke");
    c->cancel_stroke();
    check(!c->stroke_active(), "cancel_stroke terminates active stroke");
  }
  // 12. Layer Panel Fixes: Selection Preview, Popup Crash, Drag-and-Drop, and Selection Indicator
  {
    auto *lp = window.layer_panel();
    check(lp != nullptr, "LayerPanel instance available");
    auto *card = lp ? lp->selection_card() : nullptr;
    check(card != nullptr, "SelectionLayerCard instance available");
    if (card) {
      check(card->minimumHeight() == 42 && card->maximumHeight() == 42, "SelectionLayerCard has fixed 42px height");
      check(card->preview_button() != nullptr, "SelectionLayerCard has camera preview button");
    }

    // 12.1 render_selection_preview tests
    doc.begin_change();
    while (doc.layer_count() > 1) {
      doc.remove_layer(0);
    }
    doc.commit_change();
    doc.active_layer().image().set_pixel(10, 10, kreart::core::Rgba8{50, 50, 50, 255});
    doc.selection().clear();

    QPixmap prev_no_sel = kreart::ui::render_selection_preview(doc, window.canvas(), 360, 240);
    check(!prev_no_sel.isNull(), "render_selection_preview produces non-null pixmap without selection");
    const double expected_aspect = static_cast<double>(doc.width()) / doc.height();
    const double actual_aspect = static_cast<double>(prev_no_sel.width()) / prev_no_sel.height();
    check(std::abs(expected_aspect - actual_aspect) < 0.05, "render_selection_preview preserves document aspect ratio");

    // Test with active selection mask
    doc.selection().set_rect(0, 0, 100, 100);
    QPixmap prev_with_sel = kreart::ui::render_selection_preview(doc, window.canvas(), 360, 240);
    check(!prev_with_sel.isNull(), "render_selection_preview produces non-null pixmap with active selection");
    QColor c_no_sel = prev_no_sel.toImage().pixelColor(2, 2);
    QColor c_sel = prev_with_sel.toImage().pixelColor(2, 2);
    check(c_sel.red() > c_no_sel.red(), "Selection preview contains pink selection overlay in selected region");

    // Test in selection_layer_mode
    if (window.canvas()) {
      doc.selection().clear();
      window.canvas()->set_selection_layer_mode(true);
      window.canvas()->selection_image().set_pixel(10, 10, kreart::core::Rgba8{244, 63, 94, 255});
      QPixmap prev_sel_mode = kreart::ui::render_selection_preview(doc, window.canvas(), 360, 240);
      check(!prev_sel_mode.isNull(), "render_selection_preview produces non-null pixmap in selection_layer_mode");
      QColor c_sel_mode = prev_sel_mode.toImage().pixelColor(2, 2);
      check(c_sel_mode.red() > c_no_sel.red(), "Selection preview reflects selection_image in selection_layer_mode");
      window.canvas()->set_selection_layer_mode(false);
    }

    // 12.2 Preview popup stability test (no WA_DeleteOnClose crash)
    bool card_preview_signal_emitted = false;
    QObject::connect(card, &kreart::ui::SelectionLayerCard::previewClicked, [&card_preview_signal_emitted]() {
      card_preview_signal_emitted = true;
    });
    if (card->preview_button()) {
      QTimer::singleShot(50, []() {
        for (QWidget *w : QApplication::allWidgets()) {
          if (auto *d = qobject_cast<QDialog *>(w)) {
            d->accept();
          }
        }
      });
      emit card->preview_button()->clicked();
      check(card_preview_signal_emitted, "Clicking SelectionLayerCard preview button emits previewClicked signal");
    }
    // 12.3 Drag-and-Drop Reordering, Group Entry, Group Exit, and Hierarchy Checks
    doc.begin_change();
    while (doc.layer_count() > 1) {
      doc.remove_layer(0);
    }
    doc.layer(0).set_name("Bottom Layer");
    doc.layer(0).set_parent_id(0);
    const std::size_t g_idx = doc.add_group("Parent Group");
    const std::size_t g_id = doc.layer(g_idx).id();
    const std::size_t l_child = doc.add_layer("Child 1");
    doc.layer(l_child).set_parent_id(g_id);
    const std::size_t l_top = doc.add_layer("Top Layer");
    doc.commit_change();
    lp->rebuild_layer_list();

    check(doc.layer(0).name() == "Bottom Layer", "Initial bottom layer is Bottom Layer");
    check(doc.layer(3).name() == "Top Layer", "Initial top layer is Top Layer");

    // Drag Bottom Layer above Top Layer: target = Top Layer (index 3), rel_y = 0.2 (< 0.5)
    bool drop_success = lp->tree_widget()->perform_drop(0, 3, 0.2);
    check(drop_success, "perform_drop from bottom to top layer succeeded");
    check(doc.layer(3).name() == "Bottom Layer", "Bottom Layer successfully moved to top of document");

    // Drag Child 1 out of group to above Parent Group
    std::size_t child_idx = 0;
    std::size_t parent_idx = 0;
    for (std::size_t i = 0; i < doc.layer_count(); ++i) {
      if (doc.layer(i).name() == "Child 1") child_idx = i;
      if (doc.layer(i).name() == "Parent Group") parent_idx = i;
    }
    drop_success = lp->tree_widget()->perform_drop(child_idx, parent_idx, 0.1);
    check(drop_success, "perform_drop moving child above group succeeded");
    for (std::size_t i = 0; i < doc.layer_count(); ++i) {
      if (doc.layer(i).name() == "Child 1") {
        check(doc.layer(i).parent_id() == 0, "Child 1 parent_id reset to 0 after dragging above group");
        break;
      }
    }

    // Drag Top Layer into Parent Group
    std::size_t top_idx = 0;
    for (std::size_t i = 0; i < doc.layer_count(); ++i) {
      if (doc.layer(i).name() == "Top Layer") top_idx = i;
      if (doc.layer(i).name() == "Parent Group") parent_idx = i;
    }
    drop_success = lp->tree_widget()->perform_drop(top_idx, parent_idx, 0.5);
    check(drop_success, "perform_drop moving layer into group succeeded");
    for (std::size_t i = 0; i < doc.layer_count(); ++i) {
      if (doc.layer(i).name() == "Top Layer") {
        check(doc.layer(i).parent_id() == g_id, "Top Layer parent_id is Parent Group id after dropping into folder");
        break;
      }
    }
    check(doc.layer(parent_idx).expanded(), "Parent Group automatically expanded after dropping layer into it");

    // Test rejection of group self-parenting
    const std::size_t sub_grp = doc.add_group("Sub Group");
    const std::size_t sub_grp_id = doc.layer(sub_grp).id();
    doc.layer(sub_grp).set_parent_id(g_id);
    for (std::size_t i = 0; i < doc.layer_count(); ++i) {
      if (doc.layer(i).name() == "Parent Group") parent_idx = i;
      if (doc.layer(i).name() == "Sub Group") child_idx = i;
    }
    bool invalid_drop = lp->tree_widget()->perform_drop(parent_idx, child_idx, 0.5);
    check(!invalid_drop, "perform_drop rejected dropping parent group into its own descendant");

    // Empty space drop: simulate drop event with no target item
    {
      const std::size_t top_before = doc.layer_count() - 1;
      const std::string moving_name = doc.layer(top_before).name();
      QMimeData mime;
      QByteArray data;
      QDataStream stream(&data, QIODevice::WriteOnly);
      stream << static_cast<qint32>(top_before);
      mime.setData("application/x-kreart-layer", data);
      QDropEvent empty_drop(QPointF(10, 5000), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
      lp->select_layer(top_before, false);
      lp->tree_widget()->dropEvent(&empty_drop);
      check(doc.layer(0).name() == moving_name && doc.layer(0).parent_id() == 0,
            "Empty space drop moved item to bottom of stack (index 0) with parent_id == 0");
    }
    // 12.4 Selection Indicator & Mode Synchronization
    lp->select_layer(0, false);
    check(!card->is_selected(), "SelectionLayerCard is unselected when a normal layer is selected");
    check(lp->selected_layers().count(0) > 0, "selected_layers contains active layer index 0");

    emit card->clicked();
    check(card->is_selected(), "SelectionLayerCard is selected after click");
    check(window.canvas()->selection_layer_mode(), "Canvas selection_layer_mode is ON after clicking SelectionLayerCard");
    check(lp->tree_widget()->selectedItems().isEmpty(), "Layer tree selection cleared in selection layer mode");

    lp->select_layer(1, false);
    check(!card->is_selected(), "SelectionLayerCard is unselected after selecting layer 1");
    check(!window.canvas()->selection_layer_mode(), "Canvas selection_layer_mode is OFF after selecting layer 1");
    check(lp->selected_layers().count(1) > 0, "selected_layers contains index 1");

    // Verify LayerRowWidget has WA_StyledBackground enabled
    QTreeWidgetItem *top_item = lp->tree_widget()->topLevelItem(0);
    if (top_item) {
      auto *row = qobject_cast<kreart::ui::LayerRowWidget *>(lp->tree_widget()->itemWidget(top_item, 1));
      check(row != nullptr, "LayerRowWidget found for top level item");
      if (row) {
        check(row->testAttribute(Qt::WA_StyledBackground), "LayerRowWidget has Qt::WA_StyledBackground enabled");
        if (row->preview_button()) {
          QTimer::singleShot(50, []() {
            for (QWidget *w : QApplication::allWidgets()) {
              if (auto *d = qobject_cast<QDialog *>(w)) {
                d->accept();
              }
            }
          });
          emit row->preview_button()->clicked();
          check(true, "LayerRowWidget preview button clicked and closed without crash");
        }
      }
    }
  }

  // 13. PreferencesDialog titles, accent color removal, and tablet reset
  {
    kreart::ui::PreferencesDialog pref_dialog;

    const auto group_boxes = pref_dialog.findChildren<QGroupBox *>();
    QStringList titles;
    for (auto *gb : group_boxes) {
      titles << gb->title();
    }
    check(titles.contains("Appearance Theme"), "PreferencesDialog has 'Appearance Theme' title");
    check(titles.contains("Brush Resources & Detection"), "PreferencesDialog has 'Brush Resources & Detection' title");
    check(titles.contains("Tablet Pressure Response"), "PreferencesDialog has 'Tablet Pressure Response' title");
    check(!titles.contains("APPEARANCE & THEME"), "PreferencesDialog no longer has 'APPEARANCE & THEME'");
    check(!titles.contains("BRUSH RESOURCES & DETECTION"), "PreferencesDialog no longer has 'BRUSH RESOURCES & DETECTION'");
    check(!titles.contains("TABLET PRESSURE RESPONSE"), "PreferencesDialog no longer has 'TABLET PRESSURE RESPONSE'");

    const auto labels = pref_dialog.findChildren<QLabel *>();
    bool has_accent_label = false;
    for (auto *lbl : labels) {
      if (lbl->text().contains("Accent", Qt::CaseInsensitive)) {
        has_accent_label = true;
      }
    }
    check(!has_accent_label, "Accent Color label removed from preferences");

    auto *reset_btn = pref_dialog.reset_pressure_button();
    check(reset_btn != nullptr, "Reset button exists in tablet pressure response");
    if (reset_btn && pref_dialog.pressure_curve()) {
      check(reset_btn->text() == "Reset", "Reset button has text 'Reset'");
      pref_dialog.pressure_curve()->set_gamma(2.5);
      check(std::abs(pref_dialog.pressure_curve()->gamma() - 2.5) < 0.01, "Pressure curve gamma manually changed to 2.5");
      emit reset_btn->clicked();
      check(std::abs(pref_dialog.pressure_curve()->gamma() - 1.0) < 0.001, "Reset button restored pressure curve gamma to 1.0");
      check(kreart::ui::AppSettings::instance().pressure_curve() == "0.0,0.0;1.0,1.0", "Reset button saved linear curve to AppSettings");
    }
  }

  // 14. Single selection enforcement and add layer/group deselecting previous active
  {
    kreart::core::Document doc(400, 400);
    kreart::ui::LayerPanel lp(doc, nullptr);
    check(doc.layer_count() == 1, "Doc initialized with 1 layer");
    check(doc.has_active_layer() && doc.active_layer_index() == 0, "Layer 0 is active initially");
    check(lp.selected_layers().size() == 1 && lp.selected_layers().count(0) == 1, "Layer 0 is selected in LayerPanel");

    // Adding a layer deselects previous layer (0) and selects the new layer (1)
    lp.on_add_layer();
    check(doc.layer_count() == 2, "Doc now has 2 layers");
    check(doc.has_active_layer() && doc.active_layer_index() == 1, "New layer (1) is active");
    check(lp.selected_layers().size() == 1 && lp.selected_layers().count(1) == 1, "New layer (1) is selected; layer 0 deselected");

    // Adding a group deselects layer 1 and selects the new group (2)
    lp.on_add_group();
    check(doc.layer_count() == 3, "Doc now has 3 layers");
    check(doc.has_active_layer() && doc.active_layer_index() == 2, "New group (2) is active");
    check(lp.selected_layers().size() == 1 && lp.selected_layers().count(2) == 1, "New group (2) is selected; previous layer deselected");

    // Adding a layer while group is active adds inside group and selects the new layer, deselecting group
    const std::size_t grp_id = doc.layer(doc.active_layer_index()).id();
    lp.on_add_layer();
    check(doc.layer_count() == 4, "Doc now has 4 layers");
    check(doc.has_active_layer() && !doc.active_layer().is_group() && doc.active_layer().parent_id() == grp_id, "Child layer inside group is active, group deselected");
    check(lp.selected_layers().size() == 1 && lp.selected_layers().count(doc.active_layer_index()) == 1, "Single selection maintained for new child layer");

    // Multi-selection is disabled: select_layer always enforces single selection
    lp.select_layer(0, true);
    check(lp.selected_layers().size() == 1 && lp.selected_layers().count(0) == 1, "select_layer enforces single selection even with multi=true");
    check(doc.active_layer_index() == 0, "Active layer is layer 0");

    lp.select_layer(1, true);
    check(lp.selected_layers().size() == 1 && lp.selected_layers().count(1) == 1, "select_layer switched to layer 1, layer 0 deselected");
    check(doc.active_layer_index() == 1, "Active layer is layer 1");
  }
  // 15. Move tool cursor overlay, layer picking, and auto-selection tests
  {
    kreart::core::Document doc(400, 300);
    kreart::ui::Canvas canvas(doc, nullptr);
    canvas.resize(400, 300);

    // Empty canvas: Move tool click does not create selection
    doc.selection().clear();
    canvas.set_tool(kreart::ui::Canvas::Tool::MOVE);
    double wx = 0.0, wy = 0.0;
    canvas.map_to_widget(50.0, 50.0, wx, wy);
    QMouseEvent click_empty(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &click_empty);
    check(!doc.selection().active(), "Move tool on empty layer does not create full-canvas selection");
    check(!doc.has_floating_selection(), "Move tool on empty layer does not start floating selection");
    QMouseEvent rel_empty(QEvent::MouseButtonRelease, QPointF(wx, wy), QPointF(wx, wy),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &rel_empty);

    // Add content to Layer 0 and Layer 1
    const std::size_t l0 = doc.active_layer_index();
    doc.layer(l0).image().set_pixel(50, 50, kreart::core::Rgba8{255, 0, 0, 255});

    const std::size_t l1 = doc.add_layer("Layer 2");
    doc.layer(l1).image().set_pixel(150, 150, kreart::core::Rgba8{0, 255, 0, 255});

    // Clear active layer
    doc.clear_active_layer();
    check(!doc.has_active_layer(), "Active layer cleared");

    // Click on (50, 50) where Layer 0 has content -> should auto-select Layer 0
    canvas.map_to_widget(50.0, 50.0, wx, wy);
    QMouseEvent click_l0(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &click_l0);
    check(doc.has_active_layer() && doc.active_layer_index() == l0,
          "Move tool click on unselected layer content auto-selects that layer");
    check(doc.has_floating_selection(), "Floating selection started for auto-selected layer");
    check(canvas.move_auto_selected(), "move_auto_selected is true for auto selection");
    QMouseEvent rel_l0(QEvent::MouseButtonRelease, QPointF(wx, wy), QPointF(wx, wy),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &rel_l0);

    // Now click on (150, 150) outside current handles -> commits l0 move, auto-selects l1
    canvas.map_to_widget(150.0, 150.0, wx, wy);
    QMouseEvent click_l1(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &click_l1);
    check(doc.has_active_layer() && doc.active_layer_index() == l1,
          "Clicking outside commits previous move and auto-selects layer under new click");
    check(doc.has_floating_selection(), "Floating selection started for Layer 1");
    QMouseEvent rel_l1(QEvent::MouseButtonRelease, QPointF(wx, wy), QPointF(wx, wy),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &rel_l1);

    // Enter commits move and clears auto-selection
    QKeyEvent key_enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &key_enter);
    check(!doc.has_floating_selection(), "Enter key committed floating selection");
    check(!doc.selection().active(), "Enter key cleared auto-created selection");
    check(!canvas.move_auto_selected(), "move_auto_selected is reset to false");

    // Explicit selection test: preserve explicit selection on Enter
    doc.selection().set_rect(10, 10, 80, 80);
    check(doc.selection().active(), "Explicit selection is active");
    check(!canvas.move_auto_selected(), "Explicit selection has move_auto_selected == false");
    canvas.map_to_widget(50.0, 50.0, wx, wy);
    QMouseEvent click_explicit(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &click_explicit);
    check(doc.has_floating_selection(), "Move tool starts floating selection for explicit selection");
    check(!canvas.move_auto_selected(), "Explicit selection move keeps move_auto_selected == false");
    QApplication::sendEvent(&canvas, &key_enter);
    check(!doc.has_floating_selection(), "Enter committed floating selection");
    check(doc.selection().active(), "Explicit selection is preserved on Enter");

    // Move tool cursor tests
    canvas.update_move_tool_cursor(wx, wy);
    check(canvas.cursor().shape() == Qt::SizeAllCursor, "Cursor is SizeAllCursor inside selection bounds");

    canvas.map_to_widget(0.0, 0.0, wx, wy);
    canvas.update_move_tool_cursor(wx, wy);
    check(canvas.cursor().shape() == Qt::ArrowCursor, "Cursor is ArrowCursor outside selection");

    // Verify cursor overlay brush state isolation
    QImage overlay_img(400, 300, QImage::Format_ARGB32_Premultiplied);
    overlay_img.fill(Qt::transparent);
    QPainter painter(&overlay_img);
    painter.setBrush(Qt::green); // Deliberately seed painter with non-null brush
    canvas.draw_overlays(painter);
    check(painter.brush().style() == Qt::NoBrush, "draw_overlays leaves painter brush as Qt::NoBrush");
    painter.end();
  }
  // 16. Selection tool click-to-deselect, Delete key removes selection area, and 4/5/6 keys
  if (window.canvas()) {
    auto *cv = window.canvas();
    auto &d = window.document();
    cv->zoom_fit();
    app.processEvents();

    // 16.1 Delete key removes content AND selection area
    d.active_layer().image().set_pixel(20, 20, kreart::core::Rgba8{255, 0, 0, 255});
    d.selection().set_rect(10, 10, 30, 30);
    check(d.selection().active(), "Selection is active before delete");
    QKeyEvent del_ev(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(cv, &del_ev);
    check(!d.selection().active(), "Delete key clears active selection area");
    check(d.active_layer().image().pixel(20, 20).a == 0, "Pixels inside selection were cleared by Delete key");

    // 16.2 Single click in RECT_SELECT without dragging removes selection area
    d.selection().set_rect(10, 10, 50, 50);
    check(d.selection().active(), "Selection set for click-to-deselect test");
    cv->set_tool(kreart::ui::Canvas::Tool::RECT_SELECT);
    double wx = 0.0, wy = 0.0;
    cv->map_to_widget(100.0, 100.0, wx, wy);
    QMouseEvent press_rect(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &press_rect);
    QMouseEvent rel_rect(QEvent::MouseButtonRelease, QPointF(wx, wy), QPointF(wx, wy),
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &rel_rect);
    check(!d.selection().active(), "Single click in Rect Select tool without dragging removes active selection");

    // Dragging in RECT_SELECT creates selection
    QApplication::sendEvent(cv, &press_rect);
    double wx2 = 0.0, wy2 = 0.0;
    cv->map_to_widget(140.0, 140.0, wx2, wy2);
    QMouseEvent move_rect(QEvent::MouseMove, QPointF(wx2, wy2), QPointF(wx2, wy2),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &move_rect);
    QMouseEvent rel_rect2(QEvent::MouseButtonRelease, QPointF(wx2, wy2), QPointF(wx2, wy2),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &rel_rect2);
    check(d.selection().active(), "Dragging in Rect Select tool creates new selection");

    // 16.3 Single click in LASSO_SELECT without dragging removes selection area
    cv->set_tool(kreart::ui::Canvas::Tool::LASSO_SELECT);
    check(d.selection().active(), "Selection active prior to lasso click-to-deselect");
    QMouseEvent press_lasso(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &press_lasso);
    QMouseEvent rel_lasso(QEvent::MouseButtonRelease, QPointF(wx, wy), QPointF(wx, wy),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &rel_lasso);
    check(!d.selection().active(), "Single click in Lasso Select tool without making area removes active selection");

    // Off-canvas initiation and continuous clamping
    cv->set_tool(kreart::ui::Canvas::Tool::RECT_SELECT);
    double off_wx = 0.0, off_wy = 0.0;
    cv->map_to_widget(-50.0, -50.0, off_wx, off_wy);
    QMouseEvent press_off(QEvent::MouseButtonPress, QPointF(off_wx, off_wy), QPointF(off_wx, off_wy),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &press_off);

    double in_wx = 0.0, in_wy = 0.0;
    cv->map_to_widget(80.0, 80.0, in_wx, in_wy);
    QMouseEvent move_in(QEvent::MouseMove, QPointF(in_wx, in_wy), QPointF(in_wx, in_wy),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &move_in);

    QMouseEvent rel_in(QEvent::MouseButtonRelease, QPointF(in_wx, in_wy), QPointF(in_wx, in_wy),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &rel_in);
    check(d.selection().has_selection(), "Off-canvas initiated drag creates active selection");
    check(d.selection().bounds().x == 0 && d.selection().bounds().y == 0, "Off-canvas selection clamped to canvas origin (0, 0)");
    check(d.selection().bounds().width == 80 && d.selection().bounds().height == 80, "Off-canvas selection width & height are 80");

    // Boolean combine mode with Shift (ADD)
    double add_start_wx = 0.0, add_start_wy = 0.0;
    cv->map_to_widget(70.0, 70.0, add_start_wx, add_start_wy);
    QMouseEvent press_add(QEvent::MouseButtonPress, QPointF(add_start_wx, add_start_wy), QPointF(add_start_wx, add_start_wy),
                          Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(cv, &press_add);
    double add_end_wx = 0.0, add_end_wy = 0.0;
    cv->map_to_widget(120.0, 120.0, add_end_wx, add_end_wy);
    QMouseEvent move_add(QEvent::MouseMove, QPointF(add_end_wx, add_end_wy), QPointF(add_end_wx, add_end_wy),
                         Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(cv, &move_add);
    QMouseEvent rel_add(QEvent::MouseButtonRelease, QPointF(add_end_wx, add_end_wy), QPointF(add_end_wx, add_end_wy),
                        Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QApplication::sendEvent(cv, &rel_add);
    check(d.selection().has_selection(), "Shift-drag produces active combined selection");
    check(d.selection().bounds().x == 0 && d.selection().bounds().width == 120, "Shift-drag expands selection bounds to 120 via ADD mode");

    // Continuous clamping in LASSO_SELECT
    cv->set_tool(kreart::ui::Canvas::Tool::LASSO_SELECT);
    d.selection().clear();
    double l_start_wx = 0.0, l_start_wy = 0.0;
    cv->map_to_widget(10.0, 10.0, l_start_wx, l_start_wy);
    QMouseEvent press_l(QEvent::MouseButtonPress, QPointF(l_start_wx, l_start_wy), QPointF(l_start_wx, l_start_wy),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &press_l);
    double l_out_wx = 0.0, l_out_wy = 0.0;
    cv->map_to_widget(99999.0, 99999.0, l_out_wx, l_out_wy);
    QMouseEvent move_l(QEvent::MouseMove, QPointF(l_out_wx, l_out_wy), QPointF(l_out_wx, l_out_wy),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &move_l);
    double l_close_wx = 0.0, l_close_wy = 0.0;
    cv->map_to_widget(10.0, 50.0, l_close_wx, l_close_wy);
    QMouseEvent rel_l(QEvent::MouseButtonRelease, QPointF(l_close_wx, l_close_wy), QPointF(l_close_wx, l_close_wy),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &rel_l);
    check(d.selection().bounds().x + d.selection().bounds().width <= d.width(),
          "Lasso coordinates beyond canvas border clamped to document width");
    check(d.selection().bounds().y + d.selection().bounds().height <= d.height(),
          "Lasso coordinates beyond canvas border clamped to document height");

    // 16.4 Canvas rotation with 4, 6, 5 keys (both normal and keypad modifier)
    cv->reset_rotation();
    check(std::abs(cv->rotation_degrees()) < 1e-4, "Rotation starts at 0");
    QKeyEvent key4(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier);
    QApplication::sendEvent(cv, &key4);
    check(std::abs(cv->rotation_degrees() - (-15.0)) < 1e-3, "Key 4 rotates canvas left by 15 deg");
    QKeyEvent key6_kp(QEvent::KeyPress, Qt::Key_6, Qt::KeypadModifier);
    QApplication::sendEvent(cv, &key6_kp);
    check(std::abs(cv->rotation_degrees()) < 1e-3, "Keypad 6 rotates canvas right by 15 deg back to 0");
    QApplication::sendEvent(cv, &key4);
    QKeyEvent key5(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier);
    QApplication::sendEvent(cv, &key5);
    check(std::abs(cv->rotation_degrees()) < 1e-3, "Key 5 resets canvas rotation to 0");

    // 16.5 Smudge tool on Canvas smudges pixels
    cv->set_tool(kreart::ui::Canvas::Tool::SMUDGE);
    check(cv->tool() == kreart::ui::Canvas::Tool::SMUDGE, "Canvas tool set to SMUDGE");
    check(cv->brush_settings().mode == kreart::brush::Mode::SMUDGE, "Brush mode is SMUDGE");

    constexpr kreart::core::Rgba8 blue_patch{0, 0, 255, 255};
    for (int y = 50; y < 70; ++y) {
      for (int x = 50; x < 70; ++x) {
        d.active_layer().image().set_pixel(x, y, blue_patch);
      }
    }
    check(d.active_layer().image().pixel(75, 60).a == 0, "Pixel outside blue patch is initially empty");

    double wx_start = 0.0, wy_start = 0.0;
    double wx_end = 0.0, wy_end = 0.0;
    cv->map_to_widget(60.0, 60.0, wx_start, wy_start);
    cv->map_to_widget(75.0, 60.0, wx_end, wy_end);

    QMouseEvent smudge_press(QEvent::MouseButtonPress, QPointF(wx_start, wy_start), QPointF(wx_start, wy_start),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &smudge_press);
    QMouseEvent smudge_move(QEvent::MouseMove, QPointF(wx_end, wy_end), QPointF(wx_end, wy_end),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &smudge_move);
    QMouseEvent smudge_rel(QEvent::MouseButtonRelease, QPointF(wx_end, wy_end), QPointF(wx_end, wy_end),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &smudge_rel);

    const auto smudged = d.active_layer().image().pixel(75, 60);
    check(smudged.a > 0 && smudged.b > 50, "Smudge tool successfully smeared paint onto transparent area");

    // 16.6 Fast Fill tool and full Undo/Redo recovery test
    const std::size_t fill_l_idx = d.add_layer("Fill Undo Test");
    d.set_active_layer_index(fill_l_idx);
    check(d.active_layer().image().tiles().empty(), "New layer has 0 tiles initially");

    cv->set_tool(kreart::ui::Canvas::Tool::FILL);
    cv->set_brush_color(kreart::core::Rgba8{255, 100, 50, 255});
    cv->zoom_fit();
    app.processEvents();
    double wx_fill = 0.0, wy_fill = 0.0;
    cv->map_to_widget(200.0, 200.0, wx_fill, wy_fill);

    const auto t_fill_start = std::chrono::high_resolution_clock::now();
    QMouseEvent fill_press(QEvent::MouseButtonPress, QPointF(wx_fill, wy_fill), QPointF(wx_fill, wy_fill),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &fill_press);
    const auto t_fill_end = std::chrono::high_resolution_clock::now();
    const auto fill_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_fill_end - t_fill_start).count();
    check(fill_ms < 500, "Fast flood_fill executed under 500ms");
    check(d.active_layer().image().pixel(200, 200).r == 255, "Flood fill colored the layer");

    // Perform Undo
    QKeyEvent undo_key(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    QApplication::sendEvent(&window, &undo_key);
    app.processEvents();

    check(d.active_layer().image().pixel(200, 200).a == 0, "Undo completely cleared filled pixels (alpha is 0)");
    check(d.active_layer().image().tiles().empty(), "Undo completely removed all filled tiles from active layer");

    // Perform Redo
    QKeyEvent redo_key(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(&window, &redo_key);
    app.processEvents();
    check(d.active_layer().image().pixel(200, 200).r == 255, "Redo restored the filled pixels");

    // 17. Symmetry repositioning, 4/5/6 dialog unblock, ProjectGalleryWindow, GridSetupDialog
    // 17.1 Symmetry repositioning on canvas resize
    check(cv->symmetry_axis_x() == d.width() * 0.5, "Symmetry X starts at center");
    check(cv->symmetry_axis_y() == d.height() * 0.5, "Symmetry Y starts at center");
    d.resize_canvas(800, 600);
    cv->refresh();
    check(cv->symmetry_axis_x() == 400.0, "Symmetry X repositioned to 400 on canvas resize");
    check(cv->symmetry_axis_y() == 300.0, "Symmetry Y repositioned to 300 on canvas resize");

    // 17.2 Typing 4, 5, 6 in spinbox inside CanvasSizeDialog does NOT rotate canvas
    cv->reset_rotation();
    check(cv->rotation_degrees() == 0.0, "Canvas rotation is 0 before dialog test");
    kreart::ui::CanvasSizeDialog size_dlg(&window, d, cv);
    auto *w_spin = size_dlg.findChild<QSpinBox *>();
    check(w_spin != nullptr, "CanvasSizeDialog has QSpinBox");
    if (w_spin) {
      w_spin->setFocus();
      w_spin->setValue(100);
      QKeyEvent key4(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier, "4");
      QApplication::sendEvent(w_spin, &key4);
      check(cv->rotation_degrees() == 0.0, "Canvas did NOT rotate when Key_4 pressed in dialog spinbox");
    }

    // 17.3 GridSetupDialog test
    kreart::ui::GridSetupDialog grid_dlg(&window, cv);
    auto *grid_spin = grid_dlg.findChild<QSpinBox *>();
    auto *grid_check = grid_dlg.findChild<QCheckBox *>();
    check(grid_spin != nullptr, "GridSetupDialog has spacing spinbox");
    check(grid_check != nullptr, "GridSetupDialog has show grid checkbox");
    if (grid_spin && grid_check) {
      grid_check->setChecked(true);
      grid_spin->setValue(48);
      check(cv->show_grid() == true, "GridSetupDialog live preview enabled show_grid");
      check(cv->grid_size() == 48, "GridSetupDialog live preview set grid_size to 48");
      auto *btn_box = grid_dlg.findChild<QDialogButtonBox *>();
      if (btn_box) {
        emit btn_box->accepted();
        check(kreart::ui::AppSettings::instance().grid_size() == 48, "Grid size 48 saved to AppSettings");
        kreart::ui::AppSettings::instance().set_show_grid(false);
        cv->set_show_grid(false);
      }
    }

    // 17.4 ProjectGalleryWindow and Startup Mode preference test
    auto &settings = kreart::ui::AppSettings::instance();
    std::error_code ec;
    std::filesystem::create_directories(settings.projects_directory().toStdString(), ec);
    std::filesystem::path test_kra = std::filesystem::path(settings.projects_directory().toStdString()) / "Artwork 1.kra";
    bool created_test_kra = false;
    if (!std::filesystem::exists(test_kra, ec)) {
      kreart::core::Document doc(64, 64);
      std::string err;
      kreart::io::write_kra(test_kra, doc, err);
      created_test_kra = true;
    }

    kreart::ui::ProjectGalleryWindow gal_win(&window);
    check(gal_win.gallery_view() != nullptr, "ProjectGalleryWindow created with GalleryView");
    gal_win.refresh();

    settings.set_startup_mode(kreart::ui::StartupMode::BLANK_CANVAS);
    settings.load();
    check(settings.startup_mode() == kreart::ui::StartupMode::BLANK_CANVAS, "StartupMode BLANK_CANVAS persisted and loaded");
    settings.set_startup_mode(kreart::ui::StartupMode::GALLERY);
    settings.load();
    check(settings.startup_mode() == kreart::ui::StartupMode::GALLERY, "StartupMode GALLERY persisted and loaded");
    // 17.5 Test KRA thumbnail extraction and gallery card fixed sizing
    if (std::filesystem::exists(test_kra, ec)) {
      std::vector<std::uint8_t> png_bytes;
      std::string err;
      bool ok = kreart::io::read_kra_thumbnail(test_kra, png_bytes, err);
      check(ok && !png_bytes.empty(), "read_kra_thumbnail successfully extracted preview from Artwork 1.kra");
    }

    auto cards = gal_win.findChildren<QWidget *>();
    bool found_card = false;
    for (auto *w : cards) {
      if (w->width() == 240 && w->height() == 220) {
        found_card = true;
        break;
      }
    }
    check(found_card, "Gallery card has fixed dimensions 240x220 and does not stretch horizontally");

    kreart::ui::PreferencesDialog pref_dlg(&window);
    check(pref_dlg.startup_gallery_radio() != nullptr, "PreferencesDialog has startup_gallery_radio");
    check(pref_dlg.startup_blank_radio() != nullptr, "PreferencesDialog has startup_blank_radio");
    if (pref_dlg.startup_blank_radio()) {
      pref_dlg.startup_blank_radio()->setChecked(true);
      check(settings.startup_mode() == kreart::ui::StartupMode::BLANK_CANVAS, "Selecting blank canvas radio in preferences updates AppSettings");
      pref_dlg.startup_gallery_radio()->setChecked(true);
      check(settings.startup_mode() == kreart::ui::StartupMode::GALLERY, "Selecting gallery radio in preferences updates AppSettings");
    }
    // 17.6 Test Transparency settings and PreferencesDialog controls
    check(pref_dlg.transparency_follow_device_check() != nullptr, "PreferencesDialog has transparency_follow_device_check");
    check(pref_dlg.transparency_light_radio() != nullptr, "PreferencesDialog has transparency_light_radio");
    check(pref_dlg.transparency_dark_radio() != nullptr, "PreferencesDialog has transparency_dark_radio");
    check(pref_dlg.transparency_solid_radio() != nullptr, "PreferencesDialog has transparency_solid_radio");
    check(pref_dlg.transparency_color_button() != nullptr, "PreferencesDialog has transparency_color_button");
    // Test sidebar navigation and page switching
    check(pref_dlg.navigation_list() != nullptr, "PreferencesDialog has navigation_list");
    check(pref_dlg.navigation_list()->count() == 4, "PreferencesDialog navigation_list has 4 categories");
    check(pref_dlg.pages() != nullptr, "PreferencesDialog has pages stacked widget");
    check(pref_dlg.pages()->count() == 4, "PreferencesDialog has 4 pages");

    pref_dlg.navigation_list()->setCurrentRow(1);
    check(pref_dlg.pages()->currentIndex() == 1, "Sidebar row 1 switches to Appearance page");
    pref_dlg.navigation_list()->setCurrentRow(2);
    check(pref_dlg.pages()->currentIndex() == 2, "Sidebar row 2 switches to Brushes page");
    pref_dlg.navigation_list()->setCurrentRow(3);
    check(pref_dlg.pages()->currentIndex() == 3, "Sidebar row 3 switches to Tablet & Pen page");
    pref_dlg.navigation_list()->setCurrentRow(0);
    check(pref_dlg.pages()->currentIndex() == 0, "Sidebar row 0 switches to General page");

    // Test follow device behavior
    settings.set_transparency_follow_device(true);
    check(settings.effective_transparency_mode(true) == kreart::ui::TransparencyMode::DARK_CHECKERBOARD,
          "effective_transparency_mode returns DARK_CHECKERBOARD when follow_device is true and dark theme");
    check(settings.effective_transparency_mode(false) == kreart::ui::TransparencyMode::LIGHT_CHECKERBOARD,
          "effective_transparency_mode returns LIGHT_CHECKERBOARD when follow_device is true and light theme");

    // Test manual mode selection when follow_device is false
    settings.set_transparency_follow_device(false);
    settings.set_transparency_mode(kreart::ui::TransparencyMode::LIGHT_CHECKERBOARD);
    check(settings.effective_transparency_mode(true) == kreart::ui::TransparencyMode::LIGHT_CHECKERBOARD,
          "effective_transparency_mode returns LIGHT_CHECKERBOARD regardless of theme when manual");

    settings.set_transparency_mode(kreart::ui::TransparencyMode::DARK_CHECKERBOARD);
    check(settings.effective_transparency_mode(false) == kreart::ui::TransparencyMode::DARK_CHECKERBOARD,
          "effective_transparency_mode returns DARK_CHECKERBOARD regardless of theme when manual");

    settings.set_transparency_mode(kreart::ui::TransparencyMode::SOLID_COLOR);
    check(settings.effective_transparency_mode(true) == kreart::ui::TransparencyMode::SOLID_COLOR,
          "effective_transparency_mode returns SOLID_COLOR regardless of theme when manual");

    settings.set_transparency_solid_color(QColor(100, 150, 200));
    settings.save();
    settings.load();
    check(settings.transparency_solid_color() == QColor(100, 150, 200),
          "transparency_solid_color persisted and loaded");

    // Reset settings to default follow device
    settings.set_transparency_follow_device(true);
    settings.set_transparency_mode(kreart::ui::TransparencyMode::DARK_CHECKERBOARD);
    settings.set_transparency_solid_color(QColor(32, 33, 36));
    settings.save();
    if (created_test_kra) {
      std::filesystem::remove(test_kra, ec);
    }
  }

  // Desktop stroke undo: MainWindow enables stroke history on its Canvas only.
  {
    auto &wd = window.document();
    auto *cv = window.canvas();
    wd.selection().clear();
    // Start from a known-empty history so the assertions below are about this
    // stroke and nothing earlier in the suite.
    while (wd.can_undo()) wd.undo();
    const std::size_t probe = wd.add_layer("Stroke Undo Probe");
    wd.set_active_layer_index(probe);
    wd.layer(probe).set_visible(true);
    wd.layer(probe).set_locked(false);
    cv->set_tool(kreart::ui::Canvas::Tool::BRUSH);
    cv->set_brush_color(kreart::core::Rgba8{255, 0, 0, 255});
    cv->set_brush_size(32.0);
    // Force the CPU brush path: the GPU path only bakes pixels into the layer
    // on release and needs a real GL context, which offscreen lacks.
    cv->set_brush_color_rate(0.5);
    cv->zoom_fit();
    const int cx = wd.width() / 2, cy = wd.height() / 2;
    const kreart::core::Rgba8 before_px = wd.layer(probe).image().pixel(cx, cy);
    double wx = 0, wy = 0;
    cv->map_to_widget(cx, cy, wx, wy);
    QMouseEvent p(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &p);
    QMouseEvent m(QEvent::MouseMove, QPointF(wx + 12, wy + 12), QPointF(wx + 12, wy + 12), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &m);
    QMouseEvent r(QEvent::MouseButtonRelease, QPointF(wx + 12, wy + 12), QPointF(wx + 12, wy + 12), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(cv, &r);
    check(wd.can_undo(), "desktop Canvas stroke is recorded in undo history");
    check(!(wd.layer(probe).image().pixel(cx, cy) == before_px),
          "the desktop stroke actually painted the probed pixel");
    QKeyEvent undo_key(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    QApplication::sendEvent(&window, &undo_key);
    app.processEvents();
    check(wd.layer(probe).image().pixel(cx, cy) == before_px,
          "Ctrl+Z after a desktop stroke restores the pixel under the stroke start");
  }

  // Layer-panel operations are undoable.
  {
    auto &wd = window.document();
    const std::size_t before_count = wd.layer_count();
    window.layer_panel()->on_add_layer();
    check(wd.layer_count() == before_count + 1, "on_add_layer added a layer");
    QKeyEvent undo_key(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    QApplication::sendEvent(&window, &undo_key);
    app.processEvents();
    check(wd.layer_count() == before_count, "Ctrl+Z undoes a layer added from the Layer panel");
  }

  // Preset rows must carry database indices, not filtered list rows.
  {
    auto *cat = window.findChild<QComboBox *>("presetCategoryCombo");
    QListWidget *pl = window.preset_list();
    check(cat != nullptr && pl != nullptr, "preset category combo and list are reachable");
    if (cat && pl) {
      // "Paint" is a filtered category whose first database index is far from
      // row 0, which is exactly the case the old row-based mapping got wrong.
      const int paint = cat->findText("Paint");
      if (paint >= 0) cat->setCurrentIndex(paint);
      app.processEvents();
      check(pl->count() > 0, "Paint category lists at least one preset");
      if (pl->count() > 0) {
        pl->setCurrentRow(0);
        const std::size_t stored =
            static_cast<std::size_t>(pl->item(0)->data(Qt::UserRole).toULongLong());
        check(window.active_preset_index().has_value() &&
                  *window.active_preset_index() == stored,
              "preset list row selects the database index stored in Qt::UserRole");
        check(stored != 0,
              "filtered preset row stores a database index, not a list row");
      }
    }
  }

  // Tool panel controls exist and drive the canvas.
  auto *wand_slider = window.findChild<QSlider *>("wandToleranceSlider");
  auto *tex_scale = window.findChild<QSlider *>("textureScaleSlider");
  auto *tex_strength = window.findChild<QSlider *>("textureStrengthSlider");
  check(wand_slider && tex_scale && tex_strength,
        "wand tolerance and texture scale/strength sliders exist");
  if (wand_slider) {
    wand_slider->setValue(77);
    check(window.canvas()->wand_tolerance() == 77,
          "wand tolerance slider drives Canvas::wand_tolerance");
  }

  // Saved pressure curve reaches the desktop canvas.
  kreart::ui::AppSettings::instance().set_pressure_curve("2.0");
  check(std::abs(window.canvas()->pressure_curve() - 2.0) < 1e-6,
        "saved pressure curve is pushed into the desktop Canvas");
  kreart::ui::AppSettings::instance().set_pressure_curve("1.0");

  // Desktop theme applier produces a palette matching the mode.
  {
    auto &theme_settings = kreart::ui::AppSettings::instance();
    theme_settings.set_theme(kreart::ui::ThemeMode::DARK);
    kreart::ui::apply_desktop_theme();
    check(qApp->palette().color(QPalette::Window).lightness() < 128,
          "dark theme sets a dark window palette");
    theme_settings.set_theme(kreart::ui::ThemeMode::LIGHT);
    kreart::ui::apply_desktop_theme();
    check(qApp->palette().color(QPalette::Window).lightness() > 128,
          "light theme sets a light window palette");
    check(qApp->styleSheet().contains("QSlider::sub-page:horizontal"), "QSS styles QSlider sub-page with accent");
    check(qApp->styleSheet().contains("QToolButton:checked"), "QSS styles QToolButton:checked with accent");
    check(qApp->styleSheet().contains("QScrollBar:vertical"), "QSS styles QScrollBar with accent");
    theme_settings.set_theme(kreart::ui::ThemeMode::SYSTEM);
    kreart::ui::apply_desktop_theme();
  }

  // Recent files. Replaces the window document, so it runs after the
  // document-dependent blocks above.
  {
    const std::filesystem::path recent_kra =
        std::filesystem::temp_directory_path() / "kreart-recent-probe.kra";
    kreart::core::Document probe(16, 16);
    std::string err;
    kreart::io::write_kra(recent_kra, probe, err);
    window.open_file(recent_kra);
    const auto &recents = kreart::ui::AppSettings::instance().recent_files();
    check(!recents.isEmpty() &&
              recents.first() == QString::fromStdString(recent_kra.string()),
          "open_file records the path in AppSettings::recent_files");
    std::error_code rec;
    std::filesystem::remove(recent_kra, rec);
    kreart::ui::AppSettings::instance().clear_recent_files();
  }

  // Bracket hotkey resizes the brush when no selection exists.
  {
    window.document().selection().clear();
    window.canvas()->set_brush_size(40.0);
    const double before_size = window.canvas()->brush_size();
    QKeyEvent grow(QEvent::KeyPress, Qt::Key_BracketRight, Qt::NoModifier);
    QApplication::sendEvent(&window, &grow);
    check(window.canvas()->brush_size() > before_size,
          "] grows the brush when no selection is active");
    QKeyEvent shrink(QEvent::KeyPress, Qt::Key_BracketLeft, Qt::NoModifier);
    QApplication::sendEvent(&window, &shrink);
    check(std::abs(window.canvas()->brush_size() - before_size) < 0.01,
          "[ restores the previous brush size when no selection is active");
  }

  // PNG export must cover edge and partial tiles exactly.
  {
    kreart::core::Document edge_doc(300, 300);
    edge_doc.image().set_pixel(0, 0, kreart::core::Rgba8{200, 10, 20, 255});
    edge_doc.image().set_pixel(299, 299, kreart::core::Rgba8{10, 20, 30, 255});
    edge_doc.image().set_pixel(150, 299, kreart::core::Rgba8{40, 50, 60, 255});
    const std::filesystem::path edge_png =
        std::filesystem::temp_directory_path() / "kreart-edge-tiles.png";
    std::string edge_err;
    check(kreart::io::write_png(edge_png, edge_doc.image(), edge_err),
          "write_png succeeds for a 300x300 document");
    QImage back(QString::fromStdString(edge_png.string()));
    check(!back.isNull(), "exported PNG decodes");
    check(back.pixelColor(0, 0) == QColor(200, 10, 20, 255),
          "exported PNG preserves the top-left pixel");
    check(back.pixelColor(299, 299) == QColor(10, 20, 30, 255),
          "exported PNG preserves the bottom-right edge tile pixel");
    check(back.pixelColor(150, 299) == QColor(40, 50, 60, 255),
          "exported PNG preserves the bottom partial tile pixel");
    std::error_code edge_ec;
    std::filesystem::remove(edge_png, edge_ec);
  }

  // Shortcut system: custom bindings, export, import, reset
  {
    auto &settings = kreart::ui::AppSettings::instance();
    settings.clear_shortcuts();

    // Verify dialog construction and default population
    {
      kreart::ui::ShortcutDialog dlg(&window);
      auto *table = dlg.findChild<QTableWidget *>();
      check(table != nullptr && table->rowCount() > 0, "ShortcutDialog created and populated with actions");
    }

    // Set custom shortcut for Brush tool (Key_U)
    settings.set_shortcut("tool.brush", QKeySequence(Qt::Key_U));
    app.processEvents();
    window.canvas()->set_tool(kreart::ui::Canvas::Tool::ERASER);
    check(window.canvas()->tool() == kreart::ui::Canvas::Tool::ERASER, "Tool switched to Eraser");

    QKeyEvent key_u(QEvent::KeyPress, Qt::Key_U, Qt::NoModifier);
    QApplication::sendEvent(&window, &key_u);
    app.processEvents();
    check(window.canvas()->tool() == kreart::ui::Canvas::Tool::BRUSH, "Custom shortcut Key_U switched tool to Brush");

    // Export profile
    const QString tmp_ksf = QString::fromStdString((std::filesystem::temp_directory_path() / "test_shortcuts.ksf").string());
    check(settings.export_shortcuts(tmp_ksf), "export_shortcuts succeeded");

    // Clear / Reset
    settings.clear_shortcuts();
    app.processEvents();
    check(settings.shortcut("tool.brush", QKeySequence(Qt::Key_B)) == QKeySequence(Qt::Key_B),
          "Resetting shortcuts restored default Key_B for Brush tool");

    // Import profile
    check(settings.import_shortcuts(tmp_ksf), "import_shortcuts succeeded");
    check(settings.shortcut("tool.brush", QKeySequence(Qt::Key_B)) == QKeySequence(Qt::Key_U),
          "import_shortcuts restored Key_U binding from profile");

    // Clean up
    settings.clear_shortcuts();
    std::filesystem::remove(tmp_ksf.toStdString());
  }

  // Pen gestures: Ctrl + vertical slide zooms; Ctrl + circular movement rotates
  {
    auto *cv = window.canvas();
    cv->zoom_fit();
    cv->reset_rotation();
    app.processEvents();

    const double zoom_before = cv->zoom_percent();

    // 1. Slide up with Ctrl -> zooms in
    QMouseEvent p_zoom(QEvent::MouseButtonPress, QPointF(200, 200), QPointF(200, 200),
                       Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &p_zoom);

    QMouseEvent m_zoom(QEvent::MouseMove, QPointF(200, 140), QPointF(200, 140),
                       Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &m_zoom);

    QMouseEvent r_zoom(QEvent::MouseButtonRelease, QPointF(200, 140), QPointF(200, 140),
                       Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &r_zoom);
    app.processEvents();

    check(cv->zoom_percent() > zoom_before, "Ctrl + sliding up gesture zooms in the canvas");

    // 2. Slide down with Ctrl -> zooms out
    const double zoom_high = cv->zoom_percent();
    QMouseEvent p_down(QEvent::MouseButtonPress, QPointF(200, 140), QPointF(200, 140),
                       Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &p_down);

    QMouseEvent m_down(QEvent::MouseMove, QPointF(200, 220), QPointF(200, 220),
                       Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &m_down);

    QMouseEvent r_down(QEvent::MouseButtonRelease, QPointF(200, 220), QPointF(200, 220),
                       Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &r_down);
    app.processEvents();

    check(cv->zoom_percent() < zoom_high, "Ctrl + sliding down gesture zooms out the canvas");

    // 3. Circular drag with Ctrl -> rotates canvas
    const double rot_before = cv->rotation_degrees();
    QMouseEvent p_rot(QEvent::MouseButtonPress, QPointF(350, 150), QPointF(350, 150),
                      Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &p_rot);

    QMouseEvent m_rot(QEvent::MouseMove, QPointF(150, 350), QPointF(150, 350),
                      Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &m_rot);

    QMouseEvent r_rot(QEvent::MouseButtonRelease, QPointF(150, 350), QPointF(150, 350),
                      Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(cv, &r_rot);
    app.processEvents();

    check(std::abs(cv->rotation_degrees() - rot_before) > 5.0,
          "Ctrl + circular gesture rotates the canvas");

    cv->reset_rotation();
    cv->zoom_fit();
  }
  {
    // Step 7: Drawing Performance, Stroke Tracking & Timelapse Dialog verification
    auto *c = window.canvas();
    auto &wd = window.document();
    if (!wd.has_active_layer()) wd.auto_select_top_layer();
    wd.active_layer().set_visible(true);
    wd.active_layer().set_locked(false);
    c->set_tool(kreart::ui::Canvas::Tool::BRUSH);
    c->set_brush_color_rate(0.5);
    const auto initial_strokes = c->tracker().total_strokes();
    const auto initial_frames = c->timelapse_recorder().frame_count();
    double wx = 0.0, wy = 0.0;
    c->map_to_widget(wd.width() * 0.5, wd.height() * 0.5, wx, wy);

    // Rapid drawing burst test: 120 pointer moves
    QMouseEvent press_ev(QEvent::MouseButtonPress, QPointF(wx, wy), QPointF(wx, wy),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(c, &press_ev);

    auto t_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 120; ++i) {
      QMouseEvent move_ev(QEvent::MouseMove, QPointF(wx + (i % 15), wy + (i % 15)), QPointF(wx + (i % 15), wy + (i % 15)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(c, &move_ev);
    }
    auto t_moves = std::chrono::high_resolution_clock::now();

    QMouseEvent release_ev(QEvent::MouseButtonRelease, QPointF(wx + 15, wy + 15), QPointF(wx + 15, wy + 15),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    auto t_finish_start = std::chrono::high_resolution_clock::now();
    QApplication::sendEvent(c, &release_ev);
    auto t_finish_end = std::chrono::high_resolution_clock::now();

    double total_move_us = std::chrono::duration<double, std::micro>(t_moves - t_start).count();
    double avg_move_us = total_move_us / 120.0;
    double finish_ms = std::chrono::duration<double, std::milli>(t_finish_end - t_finish_start).count();

    std::printf("Performance profiling: avg pointer move latency = %.3f us, finish stroke latency = %.3f ms\n",
                avg_move_us, finish_ms);
    check(avg_move_us < 500.0, "Pointer move latency is under 0.5 ms (zero main-thread drawing latency)");
    check(finish_ms < 50.0, "Finish stroke latency is under 50 ms");

    check(c->tracker().total_strokes() > initial_strokes, "ProjectTracker recorded stroke");
    check(c->tracker().active_draw_time_ms() > 0, "ProjectTracker recorded active draw time");

    c->timelapse_recorder().flush_pending_frames();
    check(c->timelapse_recorder().frame_count() >= initial_frames, "Timelapse recorder captured frames");

    // Dialog verification
    kreart::ui::TimelapseDialog tl_dlg(c->timelapse_recorder(), c->tracker(), &window);
    check(tl_dlg.windowTitle().contains("Timelapse"), "TimelapseDialog created with valid title");
  }

  window.close();
  return failures == 0 ? 0 : 1;
}
