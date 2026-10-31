#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <numbers>

#include "kreart/brush/round_brush.h"
#include "kreart/core/document.h"
#include "kreart/core/tiled_image.h"
#include "kreart/io/png.h"
#include "kreart/io/kra.h"
#include "kreart/io/autosave.h"
#include "kreart/core/filters.h"
#include "kreart/core/project_tracker.h"
#include "kreart/io/wal_journal.h"
#include "kreart/io/recovery.h"
#include "kreart/io/timelapse.h"
#include "kreart/io/mp4_exporter.h"

namespace {

int failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

} // namespace

int main() {
  using kreart::core::Document;
  using kreart::core::Rgba8;
  using kreart::core::TiledImage;

  constexpr Rgba8 white{255, 255, 255, 255};
  constexpr Rgba8 red{255, 0, 0, 255};

  TiledImage image(300, 180);
  image.fill(white);
  image.clear_dirty();
  check(image.pixel(0, 0).r == 255 && image.pixel(299, 179).r == 255, "image is filled white");
  check(image.pixel(300, 10).a == 0, "out-of-bounds reads are transparent");

  const auto before = image.snapshot();
  image.set_pixel(128, 130, red);
  check(image.pixel(128, 130).r == 255 && image.pixel(128, 130).g == 0, "pixel write works");
  check(image.tile_at(1, 1) != nullptr, "cross-tile write allocates the expected tile");
  check(image.dirty_tiles().size() == 1, "only the modified tile is dirty");
  check(before.tiles.at(TiledImage::key_for(1, 1))->pixels[2 * 128 + 0].r == 255,
        "snapshot tile is isolated by copy-on-write");

  image.restore(before);
  check(image.pixel(128, 130).r == 255 && image.pixel(128, 130).g == 255,
        "snapshot restore recovers the old pixel");

  Document document(220, 160);
  document.begin_change();
  document.image().set_pixel(10, 10, red);
  document.commit_change();
  check(document.can_undo(), "document can undo after a change");
  check(document.modified(), "document is modified after a change");
  document.undo();
  check(document.image().pixel(10, 10).a == 0, "document undo restores the canvas");
  document.redo();
  check(document.image().pixel(10, 10).g == 0, "document redo restores the change");

  document.begin_change();
  check(!document.commit_change(), "an unchanged stroke does not add undo history");
  // Multi-layer tests
  Document multi_doc(100, 100);
  check(multi_doc.layer_count() == 1, "document starts with 1 layer");
  check(multi_doc.layer(0).name() == "1", "initial layer is 1");
  check(multi_doc.composite_image().pixel(50, 50).a == 0, "composite of initial layer is transparent");

  multi_doc.layer(0).image().fill(white);
  multi_doc.begin_change();
  std::size_t paint_idx = multi_doc.add_layer("Paint 1");
  check(multi_doc.layer_count() == 2, "layer count is 2 after add_layer");
  check(multi_doc.active_layer_index() == paint_idx, "active layer is new layer");
  multi_doc.layer(paint_idx).image().set_pixel(50, 50, Rgba8{255, 0, 0, 255}); // Red opaque
  multi_doc.commit_change();
  check(multi_doc.composite_image().pixel(50, 50).r == 255 &&
            multi_doc.composite_image().pixel(50, 50).g == 0,
        "composite reflects active layer red pixel over background");

  // Opacity test
  multi_doc.begin_change();
  multi_doc.set_layer_opacity(paint_idx, 0.5);
  multi_doc.commit_change();
  // 50% red over opaque white gives red ~255, green ~127, blue ~127
  Rgba8 blend_px = multi_doc.composite_image().pixel(50, 50);
  check(blend_px.r == 255 && blend_px.g > 100 && blend_px.g < 150,
        "layer opacity blends properly in composite");

  // Visibility test
  multi_doc.set_layer_visible(paint_idx, false);
  check(multi_doc.composite_image().pixel(50, 50).g == 255,
        "hiding layer restores background color in composite");
  multi_doc.set_layer_visible(paint_idx, true);

  // Undo restoring layer state
  multi_doc.undo(); // undo opacity change
  check(multi_doc.layer(paint_idx).opacity() == 1.0, "undo restores layer opacity");
  multi_doc.undo(); // undo add_layer
  check(multi_doc.layer_count() == 1, "undo restores layer count");
  multi_doc.redo(); // redo add_layer
  check(multi_doc.layer_count() == 2, "redo restores added layer");
  std::size_t grp_idx = multi_doc.add_group();
  check(multi_doc.layer(grp_idx).is_group(), "added layer is a group/folder");
  check(multi_doc.layer(grp_idx).name() == "Folder 1", "first folder named Folder 1");
  multi_doc.rename_layer(grp_idx, "Lineart Group");
  check(multi_doc.layer(grp_idx).name() == "Lineart Group", "rename_layer updates layer name");

  // Incremental composite dirty propagation test
  Document live_doc(100, 100);
  live_doc.composite_image().clear_dirty();
  check(live_doc.composite_image().dirty_tiles().empty(), "composite starts clean");
  live_doc.image().set_pixel(10, 10, red);
  check(!live_doc.image().dirty_tiles().empty(), "layer tile is dirty after drawing");
  check(!live_doc.composite_image().dirty_tiles().empty(),
        "composite_image marks dirty tiles immediately when drawing on a layer");


  kreart::brush::RoundBrush brush;
  kreart::brush::Settings settings;
  settings.size = 48.0;
  settings.opacity = 1.0;
  settings.hardness = 0.8;
  settings.color = red;
  brush.set_settings(settings);

  Document brush_document(320, 220);
  brush.begin(brush_document.image(), {60.0, 80.0}, 0.7);
  brush.move_to(brush_document.image(), {250.0, 120.0}, 1.0);
  brush.end();
  check(brush_document.image().pixel(60, 80).r == 255 &&
            brush_document.image().pixel(60, 80).g < 32,
        "brush stamps at the beginning of a stroke");
  check(brush_document.image().pixel(160, 100).r == 255, "brush stamps along the stroke path");
  check(brush_document.image().pixel(300, 200).a == 0, "brush does not paint the whole canvas");
  kreart::brush::Settings pressure_settings = settings;
  pressure_settings.size = 48.0;
  pressure_settings.hardness = 1.0;
  pressure_settings.tilt_enabled = false;

  TiledImage faint_image(120, 120);
  faint_image.fill(white);
  TiledImage strong_image(120, 120);
  strong_image.fill(white);

  kreart::brush::RoundBrush faint_brush;
  faint_brush.set_settings(pressure_settings);
  faint_brush.begin(faint_image, {60.0, 60.0}, 0.1);
  faint_brush.end();

  kreart::brush::RoundBrush strong_brush;
  strong_brush.set_settings(pressure_settings);
  strong_brush.begin(strong_image, {60.0, 60.0}, 1.0);
  strong_brush.end();

  check(faint_image.pixel(73, 60).g == 255,
        "low pressure produces a smaller dab than full pressure");
  check(strong_image.pixel(73, 60).g == 0, "full pressure reaches the same radius");

  TiledImage mid_image(120, 120);
  mid_image.fill(white);
  kreart::brush::RoundBrush mid_brush;
  mid_brush.set_settings(pressure_settings);
  mid_brush.begin(mid_image, {60.0, 60.0}, 0.5);
  mid_brush.end();
  check(mid_image.pixel(73, 60).g < 255, "linear pressure reaches the low-mid radius");

  kreart::brush::Settings curved_settings = pressure_settings;
  curved_settings.pressure_curve = 2.0;
  TiledImage curved_image(120, 120);
  curved_image.fill(white);
  kreart::brush::RoundBrush curved_brush;
  curved_brush.set_settings(curved_settings);
  curved_brush.begin(curved_image, {60.0, 60.0}, 0.5);
  curved_brush.end();
  check(curved_image.pixel(60, 60).g < 255, "pressure curve still paints at the dab center");
  check(curved_image.pixel(73, 60).g == 255,
        "a higher pressure-curve gamma produces a smaller mid-pressure dab");

  kreart::brush::Settings tilt_settings = pressure_settings;
  tilt_settings.tilt_enabled = true;
  tilt_settings.tilt_strength = 0.65;

  TiledImage tilted_image(120, 120);
  tilted_image.fill(white);
  kreart::brush::RoundBrush tilt_brush;
  tilt_brush.set_settings(tilt_settings);
  tilt_brush.begin(tilted_image, {60.0, 60.0}, 1.0, 1.0, 0.0);
  tilt_brush.end();

  check(tilted_image.pixel(88, 60).g == 0, "tilt stretches the dab along the tilt axis");
  check(tilted_image.pixel(60, 80).g == 255, "tilt narrows the dab across the tilt axis");

  kreart::brush::Settings jump_settings = pressure_settings;
  jump_settings.size = 16.0;
  jump_settings.tilt_enabled = false;

  TiledImage jump_image(140, 140);
  jump_image.fill(white);
  kreart::brush::RoundBrush jump_brush;
  jump_brush.set_settings(jump_settings);
  jump_brush.begin(jump_image, {20.0, 20.0}, 1.0);
  jump_brush.jump_to(jump_image, {120.0, 120.0}, 1.0);
  jump_brush.end();

  check(jump_image.pixel(70, 70).g == 255,
        "jump_to starts a new segment without connecting the gap");
  check(jump_image.pixel(120, 120).g == 0, "jump_to stamps at the new point");

  TiledImage edge_image(140, 100);
  edge_image.fill(white);
  kreart::brush::RoundBrush edge_brush;
  edge_brush.set_settings(jump_settings);
  edge_brush.begin(edge_image, {-30.0, 50.0}, 1.0);
  edge_brush.move_to(edge_image, {70.0, 50.0}, 1.0);
  edge_brush.end();

  check(edge_image.pixel(0, 50).g == 0,
        "a stroke starting outside paints continuously at the document edge");

  TiledImage reentry_image(140, 100);
  reentry_image.fill(white);
  kreart::brush::RoundBrush reentry_brush;
  reentry_brush.set_settings(jump_settings);
  reentry_brush.begin(reentry_image, {70.0, 50.0}, 1.0);
  reentry_brush.move_to(reentry_image, {160.0, 50.0}, 1.0);
  reentry_brush.move_to(reentry_image, {120.0, 50.0}, 1.0);
  reentry_brush.end();

  check(reentry_image.pixel(135, 50).g == 0,
        "a stroke leaving and re-entering paints continuously near the edge");

  kreart::brush::Settings eraser_settings = pressure_settings;
  eraser_settings.size = 32.0;
  eraser_settings.mode = kreart::brush::Mode::ERASE;

  TiledImage eraser_image(100, 100);
  eraser_image.fill(white);
  kreart::brush::RoundBrush eraser;
  eraser.set_settings(eraser_settings);
  eraser.begin(eraser_image, {50.0, 50.0}, 1.0);
  eraser.end();

  check(eraser_image.pixel(50, 50).a == 0, "eraser clears alpha at the dab center");
  check(eraser_image.pixel(10, 10).a == 255, "eraser leaves pixels outside the dab intact");

  TiledImage partial_erase(16, 16);
  partial_erase.fill(white);
  partial_erase.erase_pixel(8, 8, 128);
  check(partial_erase.pixel(8, 8).a > 0 && partial_erase.pixel(8, 8).a < 255,
        "partial erasing reduces alpha without clearing it");
  // Spline stroke smoothness test
  TiledImage spline_image(200, 200);
  spline_image.fill(white);
  kreart::brush::RoundBrush spline_brush;
  kreart::brush::Settings spline_settings;
  spline_settings.size = 12.0;
  spline_settings.spacing = 0.05;
  spline_settings.color = red;
  spline_brush.set_settings(spline_settings);

  spline_brush.begin(spline_image, {50.0, 50.0}, 1.0);
  spline_brush.move_to(spline_image, {100.0, 50.0}, 1.0);
  spline_brush.move_to(spline_image, {100.0, 100.0}, 1.0);
  spline_brush.end();

  check(spline_image.pixel(50, 50).g == 0, "spline starts at first point");
  bool reached_end = false;
  for (int y = 70; y <= 100; ++y) {
    for (int x = 80; x <= 120; ++x) {
      if (spline_image.pixel(x, y).g == 0) {
        reached_end = true;
        break;
      }
    }
    if (reached_end) break;
  }
  check(reached_end, "spline reaches end segment");
  bool corner_rounded = false;
  for (int y = 55; y <= 75; ++y) {
    for (int x = 85; x <= 98; ++x) {
      if (spline_image.pixel(x, y).g < 200) {
        corner_rounded = true;
        break;
      }
    }
    if (corner_rounded) break;
  }
  check(corner_rounded, "spline curves smoothly across angular path points");


  const std::filesystem::path png_path =
      std::filesystem::temp_directory_path() / "kreart-core-test.png";
  std::string error;
  check(kreart::io::write_png(png_path, brush_document.image(), error), "PNG export succeeds");
  check(std::filesystem::exists(png_path), "PNG file exists");
  check(std::filesystem::file_size(png_path) > 100, "PNG file has content");
  std::filesystem::remove(png_path);
  // KRA read/write roundtrip test
  Document kra_doc(128, 128);
  kra_doc.layer(0).set_name("Background");
  std::size_t red_layer_idx = kra_doc.add_layer("Red Layer");
  kra_doc.layer(red_layer_idx).image().set_pixel(32, 32, Rgba8{255, 0, 0, 255});
  kra_doc.layer(red_layer_idx).set_opacity(0.8);

  const std::filesystem::path kra_path =
      std::filesystem::temp_directory_path() / "kreart-test-doc.kra";
  std::string kra_error;
  check(kreart::io::write_kra(kra_path, kra_doc, kra_error), "write_kra succeeds");
  check(std::filesystem::exists(kra_path), "KRA file exists");
  check(std::filesystem::file_size(kra_path) > 200, "KRA file has content");

  Document reloaded_doc(1, 1);
  check(kreart::io::read_kra(kra_path, reloaded_doc, kra_error), "read_kra succeeds");
  check(reloaded_doc.width() == 128 && reloaded_doc.height() == 128,
        "reloaded document dimensions match");
  check(reloaded_doc.layer_count() == 2, "reloaded document layer count matches");
  check(reloaded_doc.layer(0).name() == "Background", "reloaded layer 0 is Background");
  check(reloaded_doc.layer(1).name() == "Red Layer", "reloaded layer 1 is Red Layer");
  check(reloaded_doc.layer(1).opacity() > 0.79 && reloaded_doc.layer(1).opacity() < 0.81,
        "reloaded layer opacity matches");
  check(reloaded_doc.layer(1).image().pixel(32, 32).r == 255 &&
            reloaded_doc.layer(1).image().pixel(32, 32).g == 0,
        "reloaded layer pixel data matches");
  const std::filesystem::path test_export_png =
      std::filesystem::temp_directory_path() / "kreart-krita-export.png";
  if (std::getenv("ARTANA_TEST_KRITA_CLI") != nullptr &&
      std::system("which krita >/dev/null 2>&1") == 0) {
    const std::string cmd = "timeout 5 krita \"" + kra_path.string() +
                            "\" --export --export-filename \"" + test_export_png.string() +
                            "\" >/dev/null 2>&1";
    const int ret = std::system(cmd.c_str());
    check(ret == 0, "Krita CLI exports Kreart-written KRA file without error");
    check(std::filesystem::exists(test_export_png), "Krita exported PNG exists");
    if (std::filesystem::exists(test_export_png)) {
      check(std::filesystem::file_size(test_export_png) > 100, "Krita exported PNG has content");
      std::filesystem::remove(test_export_png);
    }
  }
  std::filesystem::remove(kra_path);

  // Test reading a real Krita template .kra file
  const std::filesystem::path template_path =
      "/usr/share/krita/templates/texture/.source/Texture256x2568bitsrgb.kra";
  if (std::filesystem::exists(template_path)) {
    Document template_doc(1, 1);
    check(kreart::io::read_kra(template_path, template_doc, kra_error),
          "reading official Krita template .kra succeeds");
    check(template_doc.width() == 256 && template_doc.height() == 256,
          "template document dimensions are 256x256");
    // Paint on the template document, save, reload and verify
    template_doc.begin_change();
    brush.begin(template_doc.image(), {100.0, 100.0}, 1.0);
    brush.move_to(template_doc.image(), {150.0, 150.0}, 1.0);
    brush.end();
    template_doc.commit_change();

    const std::filesystem::path edited_kra =
        std::filesystem::temp_directory_path() / "kreart-edited-template.kra";
    check(kreart::io::write_kra(edited_kra, template_doc, kra_error),
          "saving painted Krita template succeeds");

    Document reloaded_template(1, 1);
    check(kreart::io::read_kra(edited_kra, reloaded_template, kra_error),
          "reloading painted Krita template succeeds");
    check(reloaded_template.composite_image().pixel(100, 100).r == 255,
          "painted stroke is preserved across save/reload");
    std::filesystem::remove(edited_kra);
    check(template_doc.layer_count() >= 2, "template document loaded layers");
  }
  // Autosave and crash recovery tests
  const std::filesystem::path test_orig =
      std::filesystem::temp_directory_path() / "kreart-doc.kra";
  Document orig_doc(64, 64);
  orig_doc.image().set_pixel(10, 10, Rgba8{120, 130, 140, 255});
  std::string as_error;
  check(kreart::io::write_kra(test_orig, orig_doc, as_error), "write orig doc for autosave test");

  // Modify document
  orig_doc.begin_change();
  orig_doc.image().set_pixel(10, 10, Rgba8{200, 210, 220, 255});
  orig_doc.commit_change();

  // Autosave
  std::filesystem::path saved_as_path;
  check(kreart::io::perform_autosave(orig_doc, test_orig, saved_as_path, as_error),
        "perform_autosave succeeds");
  check(std::filesystem::exists(saved_as_path), "autosave file exists on disk");

  std::filesystem::path detected_as_path;
  check(kreart::io::has_autosave(test_orig, detected_as_path), "has_autosave detects autosave");
  check(detected_as_path == saved_as_path, "detected autosave path matches");

  Document recovered_doc(1, 1);
  check(kreart::io::read_kra(detected_as_path, recovered_doc, as_error), "read recovered autosave");
  check(recovered_doc.image().pixel(10, 10).r == 200, "recovered doc has modified pixel");

  kreart::io::remove_autosave(test_orig);
  check(!kreart::io::has_autosave(test_orig, detected_as_path), "remove_autosave cleans up file");
  std::filesystem::remove(test_orig);
  // Canvas resize test
  Document resize_doc(100, 80);
  resize_doc.image().set_pixel(10, 10, red);
  check(resize_doc.width() == 100 && resize_doc.height() == 80, "initial document size");
  resize_doc.resize_canvas(140, 120, 20, 20);
  check(resize_doc.width() == 140 && resize_doc.height() == 120, "resized document dimensions");
  check(resize_doc.image().pixel(30, 30).r == 255 && resize_doc.image().pixel(30, 30).g == 0,
        "pixel shifted correctly by offset during resize");
  check(resize_doc.can_undo(), "resize adds undo history");
  resize_doc.undo();
  check(resize_doc.width() == 100 && resize_doc.height() == 80, "undo restores previous size");
  check(resize_doc.image().pixel(10, 10).r == 255, "undo restores pixel at original position");
  // SelectionMask and stroke clipping tests
  kreart::core::SelectionMask mask(100, 100);
  check(!mask.active(), "mask is initially inactive");
  check(mask.value(10, 10) == 255, "inactive mask returns 255");

  mask.set_rect(20, 20, 40, 40);
  check(mask.active(), "mask is active after set_rect");
  check(mask.is_selected(30, 30), "point inside rect is selected");
  check(!mask.is_selected(10, 10), "point outside rect is not selected");

  mask.invert();
  check(!mask.is_selected(30, 30), "point inside rect is not selected after invert");
  check(mask.is_selected(10, 10), "point outside rect is selected after invert");
  mask.clear();
  check(!mask.active(), "mask is inactive after clear");

  // Selection stroke clipping with brush
  Document sel_doc(100, 100);
  sel_doc.image().fill(kreart::core::Rgba8{255, 255, 255, 255});
  sel_doc.selection().set_rect(30, 30, 40, 40);
  brush.set_selection(&sel_doc.selection());
  brush.begin(sel_doc.image(), {10.0, 50.0}, 1.0);
  brush.move_to(sel_doc.image(), {90.0, 50.0}, 1.0);
  brush.end();

  check(sel_doc.image().pixel(15, 50).g == 255, "unselected area before rect is not painted");
  check(sel_doc.image().pixel(50, 50).r == 255 && sel_doc.image().pixel(50, 50).g < 50,
        "selected area inside rect is painted");
  check(sel_doc.image().pixel(85, 50).g == 255, "unselected area after rect is not painted");

  // Clear selection content
  sel_doc.clear_selection_content();
  check(sel_doc.image().pixel(50, 50).a == 0,
        "clear_selection_content erases pixels inside selection");
  brush.set_selection(nullptr);
  // Verify clear_selection_content does not erase when selection is inactive
  Document intact_doc(100, 100);
  intact_doc.image().set_pixel(10, 10, red);
  intact_doc.clear_selection_content();
  check(intact_doc.image().pixel(10, 10).r == 255,
        "inactive selection does not wipe canvas on clear");
  // SelectionMask translate
  kreart::core::SelectionMask trans_mask(100, 100);
  trans_mask.set_rect(10, 10, 20, 20);
  check(trans_mask.is_selected(15, 15), "point initially selected in mask");
  trans_mask.translate(30, 40);
  check(!trans_mask.is_selected(15, 15), "original point unselected after translate");
  check(trans_mask.is_selected(45, 55), "translated point selected in mask");
  check(trans_mask.bounds().x == 40 && trans_mask.bounds().y == 50, "bounds translated correctly");

  // Document move_selection with undo/redo
  Document move_doc(100, 100);
  move_doc.image().fill(kreart::core::Rgba8{0, 0, 0, 0});
  move_doc.image().set_pixel(25, 25, red);
  move_doc.selection().set_rect(20, 20, 20, 20);
  move_doc.move_selection(30, 20);
  check(move_doc.image().pixel(25, 25).a == 0, "original pixel cleared after move_selection");
  check(move_doc.image().pixel(55, 45).r == 255, "pixel moved to target in move_selection");
  check(move_doc.selection().bounds().x == 50 && move_doc.selection().bounds().y == 40,
        "selection bounds moved with pixels");
  check(move_doc.can_undo(), "move_selection recorded in undo history");
  move_doc.undo();
  check(move_doc.image().pixel(25, 25).r == 255, "undo restores original pixel location");
  check(move_doc.image().pixel(55, 45).a == 0, "undo clears target pixel");
  move_doc.redo();
  check(move_doc.image().pixel(55, 45).r == 255, "redo reapplies moved pixel");

  // Floating selection: off-canvas retention & non-destructive overlay
  Document float_doc(100, 100);
  const kreart::core::Rgba8 blue{0, 0, 255, 255};
  float_doc.image().set_pixel(25, 25, red);
  float_doc.image().set_pixel(65, 65, blue);
  float_doc.selection().set_rect(20, 20, 10, 10);
  float_doc.start_move_selection();
  check(float_doc.has_floating_selection(), "floating selection is active");

  // Drag 100px off canvas to the left
  float_doc.update_move_selection(-100, 0);
  check(float_doc.floating_selection().pixels[5 * 10 + 5].r == 255, "pixels preserved off-canvas");

  // Drag over existing blue drawing at (65, 65)
  float_doc.update_move_selection(40, 40);
  check(float_doc.composite_with_floating().pixel(65, 65).r == 255, "floating selection composites over layer");
  // Move away from blue drawing
  float_doc.update_move_selection(5, 5);
  check(float_doc.image().pixel(65, 65).b == 255, "underlying layer drawing was not destroyed");

  // Move completely off canvas and bring back
  float_doc.update_move_selection(-200, -200);
  float_doc.update_move_selection(10, 10);
  float_doc.commit_move_selection();
  check(!float_doc.has_floating_selection(), "floating selection committed");
  check(float_doc.image().pixel(35, 35).r == 255, "pixels brought back from off-canvas and committed");
  check(float_doc.image().pixel(65, 65).b == 255, "original blue pixel remains intact");

  // Dual Symmetry (Vertical + Horizontal 4-quadrant painting)
  Document dual_sym_doc(100, 100);
  dual_sym_doc.image().fill(kreart::core::Rgba8{255, 255, 255, 255});
  kreart::brush::Settings dual_settings;
  dual_settings.size = 6.0;
  dual_settings.color = red;
  dual_settings.symmetry_vertical = true;
  dual_settings.symmetry_horizontal = true;
  dual_settings.symmetry_axis_x = 50.0;
  dual_settings.symmetry_axis_y = 50.0;
  dual_settings.smoothing_mode = kreart::brush::SmoothingMode::NONE;
  kreart::brush::RoundBrush dual_brush;
  dual_brush.set_settings(dual_settings);

  dual_brush.begin(dual_sym_doc.image(), {20.0, 30.0}, 1.0);
  dual_brush.end(&dual_sym_doc.image());

  check(dual_sym_doc.image().pixel(20, 30).r == 255, "primary dab painted (top-left)");
  check(dual_sym_doc.image().pixel(80, 30).r == 255, "vertical mirror dab painted (top-right)");
  check(dual_sym_doc.image().pixel(20, 70).r == 255, "horizontal mirror dab painted (bottom-left)");
  check(dual_sym_doc.image().pixel(80, 70).r == 255, "4-quadrant mirror dab painted (bottom-right)");
  check(dual_sym_doc.image().pixel(50, 50).g == 255, "center remains unpainted");
  // Test SmoothingMode::NONE and SmoothingMode::BASIC
  Document smooth_test_doc(100, 100);
  kreart::brush::Settings none_settings;
  none_settings.size = 10.0;
  none_settings.color = red;
  none_settings.smoothing_mode = kreart::brush::SmoothingMode::NONE;
  kreart::brush::RoundBrush none_brush;
  none_brush.set_settings(none_settings);
  none_brush.begin(smooth_test_doc.image(), {10.0, 10.0}, 1.0);
  none_brush.move_to(smooth_test_doc.image(), {50.0, 10.0}, 1.0);
  none_brush.end(&smooth_test_doc.image());
  check(smooth_test_doc.image().pixel(30, 10).r == 255,
        "SmoothingMode::NONE paints stroke between points");

  kreart::brush::Settings basic_settings;
  basic_settings.size = 10.0;
  basic_settings.color = red;
  basic_settings.smoothing_mode = kreart::brush::SmoothingMode::BASIC;
  kreart::brush::RoundBrush basic_brush;
  basic_brush.set_settings(basic_settings);
  basic_brush.begin(smooth_test_doc.image(), {10.0, 30.0}, 1.0);
  basic_brush.move_to(smooth_test_doc.image(), {50.0, 30.0}, 1.0);
  basic_brush.end(&smooth_test_doc.image());
  check(smooth_test_doc.image().pixel(30, 30).r == 255,
        "SmoothingMode::BASIC paints stroke between points");


  // Magic Lasso: Auto-catch object contour
  Document magic_doc(100, 100);
  magic_doc.image().fill(kreart::core::Rgba8{0, 0, 0, 0});
  for (int y = 30; y < 50; ++y) {
    for (int x = 30; x < 50; ++x) {
      magic_doc.image().set_pixel(x, y, red);
    }
  }
  // Rough lasso loop from (10, 10) to (70, 70) enclosing the red box
  std::vector<std::pair<double, double>> rough_loop = {
      {10.0, 10.0}, {70.0, 10.0}, {70.0, 70.0}, {10.0, 70.0}};
  magic_doc.selection().add_polygon(rough_loop, &magic_doc.image(), true);
  check(magic_doc.selection().active(), "magic lasso activated selection");
  check(magic_doc.selection().bounds().x == 30 && magic_doc.selection().bounds().y == 30,
        "magic lasso snapped bounds.x and bounds.y to object");
  check(magic_doc.selection().bounds().width == 20 && magic_doc.selection().bounds().height == 20,
        "magic lasso snapped width and height to object");
  check(!magic_doc.selection().is_selected(15, 15), "empty region outside object was excluded");
  check(magic_doc.selection().is_selected(35, 35), "object pixel inside was selected");
  check(magic_doc.selection().lasso_points().size() >= 4, "magic lasso extracted contour points");
  check(magic_doc.selection().lasso_points()[0].first >= 30.0 &&
        magic_doc.selection().lasso_points()[0].first <= 50.0,
        "lasso points follow object boundary, not the rough loop");
  // Flood fill (Paint bucket)
  Document fill_doc(100, 100);
  // Draw rectangular border
  for (int x = 20; x <= 60; ++x) {
    fill_doc.image().set_pixel(x, 20, red);
    fill_doc.image().set_pixel(x, 60, red);
  }
  for (int y = 20; y <= 60; ++y) {
    fill_doc.image().set_pixel(20, y, red);
    fill_doc.image().set_pixel(60, y, red);
  }
  // use existing blue
  fill_doc.flood_fill(40, 40, blue);
  check(fill_doc.image().pixel(40, 40).b == 255, "flood fill filled interior of rectangle");
  check(fill_doc.image().pixel(10, 10).a == 0, "flood fill stayed bounded within borders");
  check(fill_doc.can_undo(), "flood fill registered undo entry");
  fill_doc.undo();
  check(fill_doc.image().pixel(40, 40).a == 0, "undo clears flood fill");
  fill_doc.redo();
  check(fill_doc.image().pixel(40, 40).b == 255, "redo restores flood fill");

  // Flood fill clipped to selection mask
  fill_doc.selection().set_rect(30, 30, 10, 10);
  const kreart::core::Rgba8 green{0, 255, 0, 255};
  fill_doc.flood_fill(35, 35, green);
  check(fill_doc.image().pixel(35, 35).g == 255, "flood fill filled selected region");
  check(fill_doc.image().pixel(45, 45).b == 255, "flood fill did not cross selection boundary");
  // Alpha Lock test
  Document alock_doc(100, 100);
  alock_doc.image().set_pixel(50, 50, red);
  alock_doc.set_layer_alpha_locked(0, true);
  // Paint over transparent area (10, 10)
  alock_doc.image().blend_pixel(10, 10, blue);
  check(alock_doc.image().pixel(10, 10).a == 0, "alpha lock prevents painting on transparent pixel");
  // Paint over existing red stroke (50, 50)
  alock_doc.image().blend_pixel(50, 50, blue);
  check(alock_doc.image().pixel(50, 50).b == 255, "alpha lock allows recoloring existing stroke");
  check(alock_doc.image().pixel(50, 50).a == 255, "alpha lock preserves original alpha");

  // 1-Click Clipping Mask test
  Document clip_doc(100, 100);
  for (int y = 40; y < 60; ++y) {
    for (int x = 40; x < 60; ++x) {
      clip_doc.layer(0).image().set_pixel(x, y, red);
    }
  }
  std::size_t clip_layer_idx = clip_doc.add_layer("Clipped Overlay");
  clip_doc.set_layer_clipping_mask(clip_layer_idx, true);
  // Fill entire clipped layer with blue
  clip_doc.layer(clip_layer_idx).image().fill(blue);
  // Check composite: inside the 20x20 base square is blue, outside is transparent!
  check(clip_doc.composite_image().pixel(50, 50).b == 255, "clipped layer renders inside base bounds");
  check(clip_doc.composite_image().pixel(10, 10).a == 0, "clipped layer is clipped outside base bounds");

  // Blend Modes test (Multiply & Screen)
  Document blend_doc(100, 100);
  blend_doc.layer(0).image().fill(kreart::core::Rgba8{255, 255, 255, 255}); // White base
  std::size_t mul_layer = blend_doc.add_layer("Multiply Layer");
  blend_doc.layer(mul_layer).image().fill(kreart::core::Rgba8{128, 128, 128, 255}); // 50% gray
  blend_doc.set_layer_composite_op(mul_layer, "multiply");
  auto mul_px = blend_doc.composite_image().pixel(50, 50);
  check(mul_px.r >= 126 && mul_px.r <= 130, "multiply blend mode darkens properly over white");

  // Screen: Red + Blue = Magenta
  blend_doc.layer(0).image().fill(red);
  blend_doc.layer(mul_layer).image().fill(blue);
  blend_doc.set_layer_composite_op(mul_layer, "screen");
  auto screen_px = blend_doc.composite_image().pixel(50, 50);
  check(screen_px.r == 255 && screen_px.b == 255 && screen_px.g == 0, "screen blend mode combines red and blue into magenta");

  // Nested folder hierarchy tests
  Document nest_doc(100, 100);
  std::size_t c2 = nest_doc.add_layer("Grandchild Layer");
  std::size_t f2 = nest_doc.add_group("Nested Folder");
  std::size_t c1 = nest_doc.add_layer("Child Layer");
  std::size_t f1 = nest_doc.add_group("Parent Folder");

  nest_doc.layer(c1).set_parent_id(nest_doc.layer(f1).id());
  nest_doc.layer(f2).set_parent_id(nest_doc.layer(f1).id());
  nest_doc.layer(c2).set_parent_id(nest_doc.layer(f2).id());

  check(nest_doc.get_layer_depth(f1) == 0, "root folder depth is 0");
  check(nest_doc.get_layer_depth(c1) == 1, "child layer depth is 1");
  check(nest_doc.get_layer_depth(f2) == 1, "nested folder depth is 1");
  check(nest_doc.get_layer_depth(c2) == 2, "grandchild layer depth is 2");

  // Collapsed ancestor check
  check(!nest_doc.is_ancestor_collapsed(c2), "grandchild not collapsed when parent expanded");
  nest_doc.layer(f1).set_expanded(false);
  check(nest_doc.is_ancestor_collapsed(c2), "grandchild collapsed when root folder collapsed");
  check(nest_doc.is_ancestor_collapsed(f2), "nested folder collapsed when root folder collapsed");

  // Invariant test: child cannot sit above parent in visual list
  nest_doc.clean_hierarchy_invariants();
  check(nest_doc.layer(c1).parent_id() == nest_doc.layer(f1).id(), "valid parent relationship preserved");
  // move_into_group tests (both dragging up and dragging down into folder)
  Document grp_test(100, 100);
  std::size_t g_l0 = 0;
  std::size_t g_f = grp_test.add_group("Folder");
  grp_test.add_layer("Top Layer");

  check(grp_test.move_into_group(g_l0, g_f), "move_into_group from below succeeds");
  check(grp_test.layer(0).parent_id() == grp_test.layer(1).id(), "layer dragged up is now inside folder");

  check(grp_test.move_into_group(2, 1), "move_into_group from above succeeds");
  check(grp_test.layer(1).parent_id() == grp_test.layer(2).id(), "layer dragged down is now inside folder");
  check(grp_test.layer(0).parent_id() == grp_test.layer(2).id(), "both layers are now inside folder");

  // Moving a folder with children into another folder keeps the children attached
  // (no orphaned children popping to the top level anymore)
  Document draggrp(100, 100);
  draggrp.add_layer("Base");                                  // idx 0 (bottom)
  const std::size_t inner_c = draggrp.add_layer("Inner Child");  // idx 1
  const std::size_t inner_f = draggrp.add_group("Inner Folder"); // idx 2
  const std::size_t outer_f = draggrp.add_group("Outer Folder"); // idx 3 (top)
  draggrp.layer(inner_c).set_parent_id(draggrp.layer(inner_f).id());
  draggrp.clean_hierarchy_invariants();
  check(draggrp.layer(inner_c).parent_id() == draggrp.layer(inner_f).id(),
        "child attached to inner folder before move");
  check(draggrp.layer(inner_f).parent_id() == 0,
        "inner folder is top-level before move");
  // Drag the Inner Folder (with its child) onto Outer Folder: children must move with it
  check(draggrp.move_into_group(inner_f, outer_f), "inner folder moves into outer folder");
  draggrp.clean_hierarchy_invariants();
  check(draggrp.layer(inner_f).parent_id() == draggrp.layer(outer_f).id(),
        "inner folder parented to outer folder after move");
  check(draggrp.layer(inner_c).parent_id() == draggrp.layer(inner_f).id(),
        "child stays attached to its folder after the folder is moved");

  // move_layer_relative: block-aware sibling reorder above/below
  Document rel(100, 100);
  rel.add_layer("Bottom");                  // idx 0: "1", idx 1: "Bottom"
  const std::size_t rb = rel.add_layer("B"); // idx 2
  const std::size_t ra = rel.add_layer("A"); // idx 3 (top)
  check(rel.layer_count() == 4, "rel doc has 4 layers");
  check(rel.move_layer_relative(ra, rb, true), "relative move below succeeds");
  check(rel.layer(2).name() == "A" && rel.layer(3).name() == "B",
        "relative move places A directly below B");
  check(rel.move_layer_relative(2, 3, false), "relative move above succeeds");
  check(rel.layer(2).name() == "B" && rel.layer(3).name() == "A",
        "relative move above keeps A directly above B");
  // Merge Layer Down test
  Document mdown_doc(100, 100);
  mdown_doc.layer(0).image().fill(kreart::core::Rgba8{0, 0, 0, 0});
  std::size_t ml1 = mdown_doc.add_layer("Layer Red");
  mdown_doc.layer(ml1).image().set_pixel(25, 25, red);
  std::size_t ml2 = mdown_doc.add_layer("Layer Blue");
  mdown_doc.layer(ml2).image().set_pixel(35, 35, blue);
  check(mdown_doc.layer_count() == 3, "mdown_doc starts with 3 layers");
  check(mdown_doc.merge_layer_down(ml2), "merge_layer_down succeeds");
  check(mdown_doc.layer_count() == 2, "layer count decreased after merge down");
  check(mdown_doc.layer(ml1).image().pixel(25, 25).r == 255, "original red pixel preserved in merged layer");
  check(mdown_doc.layer(ml1).image().pixel(35, 35).b == 255, "merged upper blue pixel present in lower layer");

  // Merge Clipper into Clipped Layer test
  Document mclip_doc(100, 100);
  for (int y = 40; y < 60; ++y) {
    for (int x = 40; x < 60; ++x) {
      mclip_doc.layer(0).image().set_pixel(x, y, red);
    }
  }
  std::size_t mclip_idx = mclip_doc.add_layer("Clipped Blue");
  mclip_doc.layer(mclip_idx).set_clipping_mask(true);
  mclip_doc.layer(mclip_idx).image().fill(blue);
  check(mclip_doc.merge_layer_down(mclip_idx), "merge clipped layer down succeeds");
  check(mclip_doc.layer_count() == 1, "merged down to 1 layer");
  check(mclip_doc.layer(0).image().pixel(50, 50).b == 255, "blue baked inside base square");
  check(mclip_doc.layer(0).image().pixel(10, 10).a == 0, "blue did not spill outside base square");

  // Merge Group test
  Document mgrp_doc(100, 100);
  std::size_t f_idx = mgrp_doc.add_group("My Folder");
  std::size_t cl1 = mgrp_doc.add_layer("Folder Child 1");
  mgrp_doc.layer(cl1).set_parent_id(mgrp_doc.layer(f_idx).id());
  mgrp_doc.layer(cl1).image().set_pixel(15, 15, red);
  std::size_t cl2 = mgrp_doc.add_layer("Folder Child 2");
  mgrp_doc.layer(cl2).set_parent_id(mgrp_doc.layer(f_idx).id());
  mgrp_doc.layer(cl2).image().set_pixel(45, 45, blue);
  check(mgrp_doc.merge_group(f_idx), "merge_group succeeds");
  check(!mgrp_doc.layer(f_idx).is_group(), "folder converted to raster layer");
  check(mgrp_doc.layer(f_idx).image().pixel(15, 15).r == 255, "child 1 pixel in merged folder");
  check(mgrp_doc.layer(f_idx).image().pixel(45, 45).b == 255, "child 2 pixel in merged folder");
  // Recursive Group Deletion and Descendant Count test
  Document rdel_doc(100, 100);
  auto find_layer = [&](const std::string &name) -> std::size_t {
    for (std::size_t i = 0; i < rdel_doc.layer_count(); ++i) {
      if (rdel_doc.layer(i).name() == name) return i;
    }
    return 0;
  };

  rdel_doc.add_group("Parent Folder");
  rdel_doc.add_layer("Child 1");
  rdel_doc.move_into_group(find_layer("Child 1"), find_layer("Parent Folder"));

  rdel_doc.add_layer("Child 2");
  rdel_doc.move_into_group(find_layer("Child 2"), find_layer("Parent Folder"));

  rdel_doc.add_group("Sub Folder");
  rdel_doc.move_into_group(find_layer("Sub Folder"), find_layer("Parent Folder"));

  const std::size_t parent_idx = find_layer("Parent Folder");
  std::size_t r_count = 0, f_count = 0;
  rdel_doc.get_group_descendants_count(parent_idx, r_count, f_count);
  check(r_count == 2, "found 2 raster child layers inside folder");
  check(f_count == 1, "found 1 sub-folder inside folder");

  check(rdel_doc.remove_group_recursive(parent_idx), "remove_group_recursive succeeds");
  check(rdel_doc.layer_count() == 1, "all descendants and parent folder removed, leaving base layer");
  check(rdel_doc.can_undo(), "recursive deletion can be undone");
  rdel_doc.undo();
  check(rdel_doc.layer_count() > 1, "undo restores folder and all children");

  // Merge Selected Layers test
  Document msel_doc(100, 100);
  std::size_t s1 = msel_doc.add_layer("S1");
  msel_doc.layer(s1).image().set_pixel(10, 10, red);
  std::size_t s2 = msel_doc.add_layer("S2");
  msel_doc.layer(s2).image().set_pixel(20, 20, blue);
  std::size_t s3 = msel_doc.add_layer("S3");
  const kreart::core::Rgba8 yellow{255, 255, 0, 255};
  msel_doc.layer(s3).image().set_pixel(30, 30, yellow);
  check(msel_doc.merge_selected_layers({s1, s2, s3}), "merge_selected_layers succeeds");
  check(msel_doc.layer(s1).image().pixel(10, 10).r == 255, "s1 pixel in merged selected");
  check(msel_doc.layer(s1).image().pixel(20, 20).b == 255, "s2 pixel in merged selected");
  check(msel_doc.layer(s1).image().pixel(30, 30).r == 255 && msel_doc.layer(s1).image().pixel(30, 30).g == 255,
        "s3 pixel in merged selected");
  // Document scale_image test (resample filters)
  Document scale_doc(100, 100);
  scale_doc.image().set_pixel(20, 20, red);
  scale_doc.selection().set_rect(20, 20, 20, 20);
  check(scale_doc.width() == 100 && scale_doc.height() == 100, "scale_doc initial size");

  // Downscale 50% with NEAREST (Pixel Art)
  scale_doc.scale_image(50, 50, kreart::core::ScaleFilter::NEAREST);
  check(scale_doc.width() == 50 && scale_doc.height() == 50, "downscaled to 50x50");
  check(scale_doc.image().pixel(10, 10).r == 255, "red pixel mapped to (10, 10) in nearest scale");
  check(scale_doc.selection().bounds().x == 10 && scale_doc.selection().bounds().width == 10,
        "selection scaled proportionally with document");

  // Undo downscale
  check(scale_doc.can_undo(), "scale_image recorded in undo history");
  scale_doc.undo();
  check(scale_doc.width() == 100 && scale_doc.height() == 100, "undo restores 100x100 size");
  check(scale_doc.image().pixel(20, 20).r == 255, "undo restores pixel at (20, 20)");

  // Downscale with BOX (Area Average)
  scale_doc.scale_image(25, 25, kreart::core::ScaleFilter::BOX);
  check(scale_doc.width() == 25 && scale_doc.height() == 25, "downscaled to 25x25 with box filter");
  check(scale_doc.image().pixel(5, 5).r > 0, "box filter preserved pixel detail in downscale");

  // Upscale 400% with BICUBIC (Catmull-Rom)
  scale_doc.scale_image(100, 100, kreart::core::ScaleFilter::BICUBIC);
  check(scale_doc.width() == 100 && scale_doc.height() == 100, "upscaled back to 100x100 with bicubic");

  // Upscale with LANCZOS3 (Sinc windowed)
  scale_doc.scale_image(200, 200, kreart::core::ScaleFilter::LANCZOS3);
  check(scale_doc.width() == 200 && scale_doc.height() == 200, "upscaled to 200x200 with lanczos3");
  // Selection Transform & Rotation test
  Document rot_doc(100, 100);
  for (int x = 40; x < 60; ++x) {
    for (int y = 48; y < 52; ++y) {
      rot_doc.active_layer().image().set_pixel(x, y, red);
    }
  }
  rot_doc.selection().set_rect(40, 48, 20, 4);
  check(rot_doc.active_layer().image().pixel(45, 50).r == 255, "horizontal stripe pixel present");

  // Rotate 90 degrees clockwise
  rot_doc.rotate_selection(std::numbers::pi / 2.0);
  check(rot_doc.has_floating_selection(), "rotate_selection activates floating selection");
  rot_doc.commit_move_selection();
  check(!rot_doc.has_floating_selection(), "commit_move_selection deactivates floating selection");

  // Now stripe is vertical around x=50, y in [40, 60)
  check(rot_doc.active_layer().image().pixel(50, 45).r == 255, "rotated 90 deg: pixel at (50, 45) is red");
  check(rot_doc.active_layer().image().pixel(42, 50).a == 0, "rotated 90 deg: pixel at (42, 50) is empty");
  check(rot_doc.selection().bounds().height >= 18, "rotated selection height is vertical");

  // Undo rotation
  check(rot_doc.can_undo(), "selection rotation recorded in history");
  rot_doc.undo();
  check(rot_doc.active_layer().image().pixel(45, 50).r == 255, "undo restored horizontal stripe");
  check(rot_doc.active_layer().image().pixel(50, 42).a == 0, "undo cleared rotated vertical pixel");

  // Redo rotation
  rot_doc.redo();
  check(rot_doc.active_layer().image().pixel(50, 45).r == 255, "redo reapplied rotated stripe");

  // Arbitrary sub-pixel rotation (45 degrees) with smooth bilinear blending
  Document smooth_rot(100, 100);
  smooth_rot.active_layer().image().set_pixel(50, 50, red);
  smooth_rot.selection().set_rect(48, 48, 5, 5);
  smooth_rot.rotate_selection(std::numbers::pi / 4.0); // 45 degrees
  smooth_rot.commit_move_selection();
  check(smooth_rot.active_layer().image().pixel(50, 50).r == 255, "45 deg rotation preserved center red pixel");

  // Scale selection test
  Document scale_sel_doc(100, 100);
  scale_sel_doc.active_layer().image().set_pixel(50, 50, red);
  scale_sel_doc.selection().set_rect(49, 49, 3, 3);
  scale_sel_doc.scale_selection(2.0, 2.0);
  check(scale_sel_doc.floating_selection().scale_x == 2.0, "scale_selection scaled x by 2x");
  scale_sel_doc.commit_move_selection();
  check(scale_sel_doc.active_layer().image().pixel(50, 50).r == 255, "scaled selection committed successfully");
  // Color Smudge & Wet Acrylic Mixing test
  Document wet_doc(100, 100);
  const kreart::core::Rgba8 pure_red{255, 0, 0, 255};
  const kreart::core::Rgba8 pure_blue{0, 0, 255, 255};
  for (int x = 20; x < 40; ++x) {
    for (int y = 20; y < 40; ++y) {
      wet_doc.active_layer().image().set_pixel(x, y, pure_red);
    }
  }

  // 1. Wet Acrylic Paint test: Paint over pure_red with pure_blue at 50% color rate
  kreart::brush::RoundBrush wet_brush;
  kreart::brush::Settings wet_s;
  wet_s.size = 16.0;
  wet_s.opacity = 1.0;
  wet_s.color = pure_blue;
  wet_s.color_rate = 0.5; // 50% brush color, 50% canvas color
  wet_s.smudge_rate = 0.5;
  wet_s.mode = kreart::brush::Mode::PAINT;
  wet_brush.set_settings(wet_s);

  wet_doc.begin_change();
  wet_brush.begin(wet_doc.active_layer().image(), {30.0, 30.0}, 1.0);
  wet_brush.end(wet_doc.active_layer().image());
  wet_doc.commit_change();

  const auto mixed_px = wet_doc.active_layer().image().pixel(30, 30);
  check(mixed_px.r > 50 && mixed_px.b > 50, "wet acrylic mixing blended red and blue into purple");

  // 2. Pure Smudge test: Smudge from red region into transparent region
  Document smudge_doc(100, 100);
  for (int x = 20; x < 40; ++x) {
    for (int y = 20; y < 40; ++y) {
      smudge_doc.active_layer().image().set_pixel(x, y, pure_red);
    }
  }
  check(smudge_doc.active_layer().image().pixel(45, 30).a == 0, "target transparent pixel initially empty");

  kreart::brush::RoundBrush smudge_brush;
  kreart::brush::Settings smudge_s;
  smudge_s.size = 16.0;
  smudge_s.opacity = 1.0;
  smudge_s.smudge_rate = 0.8;
  smudge_s.mode = kreart::brush::Mode::SMUDGE;
  smudge_brush.set_settings(smudge_s);

  smudge_doc.begin_change();
  smudge_brush.begin(smudge_doc.active_layer().image(), {35.0, 30.0}, 1.0);
  smudge_brush.move_to(smudge_doc.active_layer().image(), {45.0, 30.0}, 1.0);
  smudge_brush.end(smudge_doc.active_layer().image());
  smudge_doc.commit_change();

  const auto smudged_px = smudge_doc.active_layer().image().pixel(45, 30);
  check(smudged_px.a > 0 && smudged_px.r > 100, "smudge tool smeared red paint onto transparent area");

  // 3. Smudging blank canvas should deposit no paint
  Document empty_doc(100, 100);
  empty_doc.begin_change();
  smudge_brush.begin(empty_doc.active_layer().image(), {50.0, 50.0}, 1.0);
  smudge_brush.move_to(empty_doc.active_layer().image(), {60.0, 50.0}, 1.0);
  smudge_brush.end(empty_doc.active_layer().image());
  empty_doc.commit_change();
  check(empty_doc.active_layer().image().pixel(55, 50).a == 0, "smudging blank canvas produces no paint");
  // 4. Texture and Pattern Mask (Brush Grain) test
  auto paper_tex = kreart::brush::TextureMask::create_rough_paper(64);
  check(paper_tex != nullptr && !paper_tex->empty(), "procedural rough paper created");
  check(paper_tex->name() == "Rough Paper", "texture name is Rough Paper");

  auto canvas_tex = kreart::brush::TextureMask::create_canvas_grain(64);
  check(canvas_tex != nullptr && !canvas_tex->empty(), "procedural canvas grain created");

  auto charcoal_tex = kreart::brush::TextureMask::create_charcoal_tooth(64);
  check(charcoal_tex != nullptr && !charcoal_tex->empty(), "procedural charcoal tooth created");

  const double tex_val1 = paper_tex->sample(10.0, 10.0, 1.0);
  const double tex_val2 = paper_tex->sample(25.0, 35.0, 1.0);
  check(tex_val1 >= 0.0 && tex_val1 <= 1.0, "sample returns normalized [0, 1]");
  check(tex_val2 >= 0.0 && tex_val2 <= 1.0, "sample returns normalized [0, 1]");

  Document tex_doc(100, 100);
  kreart::brush::RoundBrush tex_brush;
  kreart::brush::Settings ts;
  ts.size = 24.0;
  ts.opacity = 1.0;
  ts.color = pure_red;
  ts.texture_enabled = true;
  ts.texture = paper_tex;
  ts.texture_strength = 0.8;
  tex_brush.set_settings(ts);

  tex_doc.begin_change();
  tex_brush.begin(tex_doc.active_layer().image(), {50.0, 50.0}, 0.5);
  tex_brush.end(tex_doc.active_layer().image());
  tex_doc.commit_change();

  int non_zero = 0;
  for (int y = 40; y <= 60; ++y) {
    for (int x = 40; x <= 60; ++x) {
      if (tex_doc.active_layer().image().pixel(x, y).a > 0) {
        ++non_zero;
      }
    }
  }
  check(non_zero > 50, "textured brush dab successfully rendered on canvas");
  // 5. Destructive Adjustment Filters
  // 5a. HSV Filter
  TiledImage hsv_img(100, 100);
  hsv_img.fill(Rgba8{255, 0, 0, 255}); // Pure red
  filter_hsv(hsv_img, 120.0, 0.0, 0.0); // +120 deg -> Green
  auto green_px = hsv_img.pixel(50, 50);
  check(green_px.g == 255 && green_px.r == 0 && green_px.b == 0,
        "filter_hsv shifts red 120 deg to pure green");

  filter_hsv(hsv_img, 0.0, -1.0, 0.0); // Desaturate fully -> Grayscale
  auto gray_px = hsv_img.pixel(50, 50);
  check(gray_px.r == gray_px.g && gray_px.g == gray_px.b,
        "filter_hsv desaturates -1.0 to neutral gray");

  // 5b. Brightness & Contrast Filter
  TiledImage bc_img(100, 100);
  bc_img.fill(Rgba8{100, 100, 100, 255});
  filter_brightness_contrast(bc_img, 20.0, 0.0);
  auto bright_px = bc_img.pixel(50, 50);
  check(bright_px.r > 140, "filter_brightness_contrast increases brightness");

  // 5c. Color Balance Filter
  TiledImage cb_img(100, 100);
  cb_img.fill(Rgba8{30, 30, 30, 255}); // Dark shadows
  kreart::core::ColorBalanceParams cb_params;
  cb_params.shadows.cyan_red = 50.0;
  filter_color_balance(cb_img, cb_params);
  auto cb_px = cb_img.pixel(50, 50);
  check(cb_px.r > cb_px.g && cb_px.r > cb_px.b,
        "filter_color_balance shifts shadows towards red");

  // 5d. Gaussian Blur Filter
  TiledImage blur_img(100, 100);
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < 50; ++x) {
      blur_img.set_pixel(x, y, Rgba8{255, 0, 0, 255});
    }
    for (int x = 50; x < 100; ++x) {
      blur_img.set_pixel(x, y, Rgba8{0, 0, 255, 255});
    }
  }
  filter_gaussian_blur(blur_img, 4.0);
  auto edge_px = blur_img.pixel(50, 50);
  check(edge_px.r > 50 && edge_px.b > 50,
        "filter_gaussian_blur convolves colors across sharp edge");

  // SelectionMask data() buffer access
  {
    Document sel_doc(100, 100);
    sel_doc.selection().set_rect(10, 10, 20, 20);
    check(sel_doc.selection().data().size() == 100 * 100, "SelectionMask::data() has width*height bytes");
    check(sel_doc.selection().data()[15 * 100 + 15] == 255, "SelectionMask::data() has 255 in selected rect");
    check(sel_doc.selection().data()[5 * 100 + 5] == 0, "SelectionMask::data() has 0 outside selected rect");
  }

  // Tier 1 & Tier 2: Selection enhancements unit tests
  {
    // Undoable selection test
    Document undo_doc(100, 100);
    undo_doc.selection().set_rect(10, 10, 30, 30);
    check(undo_doc.selection().has_selection(), "initial selection has_selection is true");
    check(undo_doc.selection().bounds().x == 10, "initial selection x is 10");

    undo_doc.begin_change();
    undo_doc.selection().set_rect(40, 40, 20, 20);
    undo_doc.commit_change();
    check(undo_doc.selection().bounds().x == 40, "selection modified to x=40");

    undo_doc.undo();
    check(undo_doc.selection().bounds().x == 10, "undo restores previous selection x=10");
    check(undo_doc.selection().bounds().width == 30, "undo restores previous selection width=30");

    undo_doc.redo();
    check(undo_doc.selection().bounds().x == 40, "redo restores selection x=40");

    // Inversion Marquee Cleanup
    kreart::core::SelectionMask inv_mask(100, 100);
    std::vector<std::pair<double, double>> poly = {{10.0, 10.0}, {40.0, 10.0}, {40.0, 40.0}, {10.0, 40.0}};
    inv_mask.add_polygon(poly);
    check(inv_mask.lasso_points().size() >= 3, "polygon has lasso_points");
    inv_mask.invert();
    check(inv_mask.has_selection(), "inverted polygon is active");
    check(inv_mask.lasso_points().empty(), "inversion drops stale polygon contour");
    check(inv_mask.bounds().width == 100 && inv_mask.bounds().height == 100, "inversion bounds spans canvas");

    // Inverting a full selection clears it
    inv_mask.select_all();
    inv_mask.invert();
    check(!inv_mask.has_selection(), "inverting full selection clears it completely");

    // Zero-glyph guard harmonization
    kreart::core::SelectionMask zero_mask(100, 100);
    zero_mask.set_rect(10, 10, 0, 0);
    check(!zero_mask.has_selection(), "zero rect is not active selection");
    zero_mask.set_rect(-20, -20, 5, 5);
    check(!zero_mask.has_selection(), "negative rect clamped to zero is not active");

    // Boolean combine modes
    kreart::core::SelectionMask bool_mask(100, 100);
    bool_mask.set_rect(10, 10, 20, 20, kreart::core::SelectionCombineMode::REPLACE);
    check(bool_mask.is_selected(15, 15), "base rect is selected");
    check(!bool_mask.is_selected(35, 15), "outside base rect is not selected");

    // ADD: combine with adjacent/overlapping rect
    bool_mask.set_rect(20, 10, 20, 20, kreart::core::SelectionCombineMode::ADD);
    check(bool_mask.is_selected(15, 15), "ADD keeps original region");
    check(bool_mask.is_selected(25, 15), "ADD selects overlapping region");
    check(bool_mask.is_selected(35, 15), "ADD selects new region");
    check(bool_mask.bounds().x == 10 && bool_mask.bounds().width == 30, "ADD updates bounds to union (10 to 40)");

    // SUBTRACT: subtract region
    bool_mask.set_rect(10, 10, 10, 20, kreart::core::SelectionCombineMode::SUBTRACT);
    check(!bool_mask.is_selected(15, 15), "SUBTRACT removes region [10..20)");
    check(bool_mask.is_selected(25, 15), "SUBTRACT keeps untouched region [20..40)");
    check(bool_mask.bounds().x == 20 && bool_mask.bounds().width == 20, "SUBTRACT updates bounds");

    // INTERSECT: intersect region
    bool_mask.set_rect(25, 10, 10, 20, kreart::core::SelectionCombineMode::INTERSECT);
    check(!bool_mask.is_selected(22, 15), "INTERSECT removes outside region");
    check(bool_mask.is_selected(30, 15), "INTERSECT keeps common region [25..35)");
    check(!bool_mask.is_selected(38, 15), "INTERSECT removes non-overlapping region");
    check(bool_mask.bounds().x == 25 && bool_mask.bounds().width == 10, "INTERSECT updates bounds");

    // Tier 3: Clipboard operations & transform tests
    Document clip_doc(100, 100);
    clip_doc.active_layer().image().set_pixel(20, 20, kreart::core::Rgba8{255, 128, 64, 255});
    clip_doc.active_layer().image().set_pixel(30, 30, kreart::core::Rgba8{10, 20, 30, 255});
    clip_doc.selection().set_rect(15, 15, 20, 20);

    // Copy
    clip_doc.copy_selection();
    check(clip_doc.clipboard().valid, "clipboard is valid after copy");
    check(clip_doc.clipboard().width == 20 && clip_doc.clipboard().height == 20, "clipboard dimensions match selection");
    check(clip_doc.clipboard().pixels[5 * 20 + 5] == kreart::core::Rgba8{255, 128, 64, 255}, "copied pixel at (20, 20) matches");

    // Paste as floating selection
    clip_doc.paste_selection();
    check(clip_doc.has_floating_selection(), "paste_selection creates active floating selection");
    check(clip_doc.floating_selection().width == 20 && clip_doc.floating_selection().height == 20, "floating selection matches clipboard size");
    check(clip_doc.floating_selection().pixels[5 * 20 + 5] == kreart::core::Rgba8{255, 128, 64, 255}, "floating selection pixels match");

    // Nudging floating selection
    clip_doc.nudge_floating(5, -3);
    check(clip_doc.floating_selection().offset_x == 5 && clip_doc.floating_selection().offset_y == -3, "nudge_floating updates offsets");

    // Flipping floating selection
    clip_doc.flip_floating_horizontal();
    check(clip_doc.floating_selection().pixels[5 * 20 + 14] == kreart::core::Rgba8{255, 128, 64, 255}, "flip_floating_horizontal mirrored pixel across x");
    clip_doc.flip_floating_vertical();
    check(clip_doc.floating_selection().pixels[14 * 20 + 14] == kreart::core::Rgba8{255, 128, 64, 255}, "flip_floating_vertical mirrored pixel across y");

    // Commit floating selection
    clip_doc.commit_move_selection();
    check(!clip_doc.has_floating_selection(), "commit_move_selection deactivates floating selection");

    // Paste as new layer
    std::size_t initial_layers = clip_doc.layer_count();
    clip_doc.paste_as_new_layer();
    check(clip_doc.layer_count() == initial_layers + 1, "paste_as_new_layer added a layer");
    check(clip_doc.active_layer().name() == "Pasted Layer", "new layer named Pasted Layer");

    // Cut
    clip_doc.set_active_layer_index(0);
    check(clip_doc.active_layer().image().pixel(20, 20) == kreart::core::Rgba8{255, 128, 64, 255}, "pixel (20, 20) exists before cut");
    clip_doc.selection().set_rect(18, 18, 10, 10);
    clip_doc.cut_selection();
    check(clip_doc.active_layer().image().pixel(20, 20).a == 0, "cut_selection cleared pixel from layer");
    check(clip_doc.clipboard().valid, "clipboard valid after cut");

    // Zero-transform commit short-circuit
    clip_doc.active_layer().image().set_pixel(50, 50, kreart::core::Rgba8{120, 130, 140, 255});
    clip_doc.selection().set_rect(48, 48, 10, 10);
    clip_doc.start_move_selection();
    check(clip_doc.has_floating_selection(), "floating selection active");
    clip_doc.commit_move_selection();
    check(!clip_doc.has_floating_selection(), "floating selection committed");
    check(clip_doc.active_layer().image().pixel(50, 50) == kreart::core::Rgba8{120, 130, 140, 255}, "zero-transform commit preserved exact pixels");

    // Tier 4: Ellipse Select, Magic Wand & Selection Modifiers
    // 1. Ellipse Select
    kreart::core::SelectionMask ell_mask(100, 100);
    ell_mask.set_ellipse(20, 20, 60, 40);
    check(ell_mask.has_selection(), "ellipse selection is active");
    check(ell_mask.is_selected(50, 40), "ellipse center is selected");
    check(ell_mask.is_selected(50, 21), "point inside ellipse is selected");
    check(!ell_mask.is_selected(20, 20), "corner outside ellipse is not selected");
    check(!ell_mask.is_selected(10, 10), "point far outside is not selected");

    // 2. Magic Wand (flood_select)
    Document wand_doc(100, 100);
    wand_doc.active_layer().image().fill(kreart::core::Rgba8{255, 255, 255, 255});
    for (int y = 30; y < 70; ++y) {
      for (int x = 30; x < 70; ++x) {
        wand_doc.active_layer().image().set_pixel(x, y, kreart::core::Rgba8{255, 0, 0, 255});
      }
    }
    wand_doc.flood_select(50, 50, 10, true);
    check(wand_doc.selection().has_selection(), "flood_select creates active selection");
    check(wand_doc.selection().is_selected(50, 50), "center of patch is selected");
    check(wand_doc.selection().is_selected(35, 35), "inside patch is selected");
    check(!wand_doc.selection().is_selected(10, 10), "outside patch is not selected");
    check(wand_doc.selection().bounds().width == 40 && wand_doc.selection().bounds().height == 40, "bounds match patch");

    // 3. Selection Modifiers: Feather, Grow, Shrink, Border
    kreart::core::SelectionMask mod_mask(100, 100);
    mod_mask.set_rect(20, 20, 40, 40);
    check(mod_mask.bounds().x == 20 && mod_mask.bounds().width == 40, "initial rect 20..60");

    // Grow
    mod_mask.grow(5);
    check(mod_mask.bounds().x == 15 && mod_mask.bounds().width == 50, "grow by 5 expands bounds by 5 on each side");
    check(mod_mask.is_selected(16, 20), "grown pixel is selected");

    // Shrink
    mod_mask.shrink(5);
    check(mod_mask.bounds().x == 20 && mod_mask.bounds().width == 40, "shrink by 5 restores bounds");

    // Feather
    mod_mask.feather(4.0);
    check(mod_mask.has_selection(), "feathered mask is active");
    const std::uint8_t edge_val = mod_mask.value(20, 20);
    check(edge_val > 0 && edge_val < 255, "feather produces soft gradient value on edge");

    // Border
    kreart::core::SelectionMask bord_mask(100, 100);
    bord_mask.set_rect(20, 20, 40, 40);
    bord_mask.border(4);
    check(bord_mask.has_selection(), "border mask is active");
    check(!bord_mask.is_selected(40, 40), "center inside border is unselected hole");
    check(bord_mask.is_selected(20, 20), "perimeter is selected in border");

    // Tier 5: Direct mask synchronization (non-collapsing painted masks)
    kreart::core::TiledImage paint_img(100, 100);
    paint_img.set_pixel(10, 10, kreart::core::Rgba8{255, 60, 150, 255});
    paint_img.set_pixel(11, 10, kreart::core::Rgba8{255, 60, 150, 200});
    paint_img.set_pixel(80, 80, kreart::core::Rgba8{255, 60, 150, 255});

    kreart::core::SelectionMask direct_mask(100, 100);
    direct_mask.set_from_tiled_image(paint_img);
    check(direct_mask.has_selection(), "direct mask is active");
    check(direct_mask.is_selected(10, 10), "blob 1 pixel selected");
    check(direct_mask.value(11, 10) == 200, "soft alpha preserved in mask");
    check(direct_mask.is_selected(80, 80), "blob 2 pixel selected");
    check(!direct_mask.is_selected(45, 45), "intermediate gap between disjoint blobs is not selected (no bbox collapse)");
    check(direct_mask.bounds().x == 10 && direct_mask.bounds().width == 71, "bounds encompass both blobs (10 to 80)");
  }

  // GPU-accelerated Brush Dab Generation, Pressure Curve, and Symmetry Test
  {
    kreart::brush::RoundBrush dab_brush;
    kreart::brush::Settings dab_settings;
    dab_settings.size = 64.0;
    dab_settings.spacing = 0.1;
    dab_settings.pressure_curve = 2.0;
    dab_settings.symmetry_vertical = true;
    dab_settings.symmetry_axis_x = 250.0;
    dab_settings.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    dab_brush.set_settings(dab_settings);

    std::vector<kreart::brush::BrushDab> dabs;
    dab_brush.generate_dabs({50.0, 50.0}, 0.5, 0.0, 0.0, dabs);
    dab_brush.generate_dabs({100.0, 100.0}, 0.8, 0.0, 0.0, dabs);

    check(!dabs.empty(), "dab count > 0 after generate_dabs");

    // Verify pressure gamma curve calculation:
    const double expected_r0 = 64.0 * 0.5 * (0.15 + 0.85 * std::pow(0.5, 2.0));
    check(std::abs(dabs[0].major_radius - expected_r0) < 0.01,
          "dab radius matches pressure gamma curve");

    // Verify symmetry: for every primary dab at (x, y), there is a mirror dab at (2 * axis_x - x, y)
    bool found_primary = false;
    bool found_mirror = false;
    for (const auto &d : dabs) {
      if (std::abs(d.x - 50.0f) < 0.01f && std::abs(d.y - 50.0f) < 0.01f) {
        found_primary = true;
      }
      if (std::abs(d.x - (2.0f * 250.0f - 50.0f)) < 0.01f && std::abs(d.y - 50.0f) < 0.01f) {
        found_mirror = true;
      }
    }
    check(found_primary, "primary dab generated at (50, 50)");
    check(found_mirror, "mirrored dab generated at (2 * axis_x - x, y) = (450, 50)");

    // Verify bounding box bounds all dab extents
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (const auto &d : dabs) {
      const float r = std::max(d.major_radius, d.minor_radius);
      min_x = std::min(min_x, d.x - r);
      min_y = std::min(min_y, d.y - r);
      max_x = std::max(max_x, d.x + r);
      max_y = std::max(max_y, d.y + r);
    }
    for (const auto &d : dabs) {
      check(d.x - d.major_radius >= min_x - 0.001f && d.x + d.major_radius <= max_x + 0.001f,
            "bounding box bounds dab x extent");
      check(d.y - d.major_radius >= min_y - 0.001f && d.y + d.major_radius <= max_y + 0.001f,
            "bounding box bounds dab y extent");
    }
  }
  // Test TiledImage::content_bounds()
  {
    TiledImage img(500, 400);
    check(!img.content_bounds().has_value(), "empty image content_bounds is nullopt");

    img.set_pixel(10, 20, red);
    auto bounds = img.content_bounds();
    check(bounds.has_value(), "single pixel image has content_bounds");
    check(bounds->x == 10 && bounds->y == 20 && bounds->width == 1 && bounds->height == 1,
          "single pixel bounds match");

    // Draw pixel across tile boundary (tile 0 is [0..127], tile 1 is [128..255])
    img.set_pixel(200, 150, red);
    bounds = img.content_bounds();
    check(bounds.has_value(), "cross-tile image has content_bounds");
    check(bounds->x == 10 && bounds->y == 20 && bounds->width == 191 && bounds->height == 131,
          "cross-tile bounds span correctly");

    // Completely erase pixels
    img.erase_pixel(10, 20, 255);
    img.erase_pixel(200, 150, 255);
    check(!img.content_bounds().has_value(), "erased image content_bounds is nullopt");
  }

  // Test Document::find_layer_at()
  {
    Document doc(200, 200);
    // Layer 0 is bottom layer
    const std::size_t l0 = doc.active_layer_index();
    doc.layer(l0).set_name("Bottom");

    const std::size_t l1 = doc.add_layer("Middle");
    const std::size_t l2 = doc.add_layer("Top");

    check(!doc.find_layer_at(50, 50).has_value(), "find_layer_at on empty canvas returns nullopt");

    doc.layer(l0).image().set_pixel(50, 50, red);
    check(doc.find_layer_at(50, 50) == l0, "find_layer_at finds bottom layer when alone");

    doc.layer(l1).image().set_pixel(50, 50, white);
    check(doc.find_layer_at(50, 50) == l1, "find_layer_at finds top-most visible layer");

    doc.layer(l1).set_visible(false);
    check(doc.find_layer_at(50, 50) == l0, "find_layer_at ignores hidden layer");

    doc.layer(l1).set_visible(true);
    doc.layer(l2).image().set_pixel(50, 50, red);
    check(doc.find_layer_at(50, 50) == l2, "find_layer_at finds newly added top layer");

    // Add group and verify group itself is not picked
    const std::size_t grp = doc.add_group("Folder");
    check(doc.find_layer_at(50, 50) == l2, "find_layer_at ignores group layer");
  }

  // Test Document batch start_move_selection and commit_move_selection
  {
    Document doc(300, 300);
    // Paint a distinct block of pixels across multiple tiles
    constexpr Rgba8 blue{0, 0, 255, 255};
    for (int y = 120; y <= 140; ++y) {
      for (int x = 120; x <= 140; ++x) {
        doc.image().set_pixel(x, y, blue);
      }
    }

    doc.selection().set_rect(120, 120, 21, 21);
    doc.start_move_selection();
    check(doc.has_floating_selection(), "floating selection is active after start_move_selection");
    check(doc.image().pixel(125, 125).a == 0, "original pixels are cleared in layer image");

    doc.update_move_selection(30, 20);
    doc.commit_move_selection();
    check(!doc.has_floating_selection(), "floating selection committed");
    check(doc.image().pixel(125, 125).a == 0, "original spot remains empty after move");
    check(doc.image().pixel(155, 145) == blue, "pixels are correctly moved to target coordinates");
    check(doc.selection().bounds().x == 150 && doc.selection().bounds().y == 140,
          "selection bounds are translated with move");
  }
  // Test Brush Mask Ratio and Asymmetric Hardness
  {
    kreart::brush::RoundBrush ratio_brush;
    kreart::brush::Settings s;
    s.size = 40.0; // radius = 20.0 at pressure 1.0
    s.ratio = 0.5;
    s.hardness = 1.0;
    s.hardness_x = 1.0;
    s.hardness_y = 1.0;
    s.color = Rgba8{255, 0, 0, 255};
    s.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    ratio_brush.set_settings(s);
    std::vector<kreart::brush::BrushDab> dabs;
    ratio_brush.generate_dabs({100.0, 100.0}, 1.0, 0.0, 0.0, dabs);
    check(!dabs.empty(), "dab generated for ratio test");
    const auto &dab = dabs[0];
    check(std::abs(dab.major_radius - 20.0f) < 0.01f, "dab major radius is 20.0");
    check(std::abs(dab.minor_radius - 10.0f) < 0.01f, "dab minor radius is 10.0 (ratio 0.5)");
    check(std::abs(dab.major_radius - 2.0f * dab.minor_radius) < 0.01f,
          "dab major radius is 2x minor radius");

    // Test CPU stamping with ratio 0.5
    TiledImage ratio_img(200, 200);
    ratio_brush.begin(ratio_img, {100.0, 100.0}, 1.0);
    ratio_brush.end();

    // At (115, 100): dx = 15 <= 20, dy = 0, so inside dab
    check(ratio_img.pixel(115, 100).a > 0, "pixel at dx=15 (within major radius 20) is painted");
    // At (100, 115): dx = 0, dy = 15 > 10, so outside dab
    check(ratio_img.pixel(100, 115).a == 0, "pixel at dy=15 (outside minor radius 10) is empty");

    // Test separate horizontal and vertical hardness (falloff)
    kreart::brush::RoundBrush fade_brush;
    kreart::brush::Settings fs;
    fs.size = 40.0; // radius = 20.0
    fs.ratio = 1.0;
    fs.hardness_x = 0.95; // hard edge horizontally
    fs.hardness_y = 0.10; // soft edge vertically
    fs.hardness = std::min(fs.hardness_x, fs.hardness_y);
    fs.color = Rgba8{0, 255, 0, 255};
    fs.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    fade_brush.set_settings(fs);

    TiledImage fade_img(200, 200);
    fade_brush.begin(fade_img, {100.0, 100.0}, 1.0);
    fade_brush.end();

    // At dist 12 along X (dx=12, dy=0): 12/20 = 0.60 <= 0.95 (hardness_x), full coverage
    auto px_x = fade_img.pixel(112, 100);
    // At dist 12 along Y (dx=0, dy=12): 12/20 = 0.60 > 0.10 (hardness_y), falloff active
    auto px_y = fade_img.pixel(100, 112);

    check(px_x.a > 0 && px_y.a > 0, "both points inside radius are painted");
    check(px_x.a > px_y.a, "harder X axis has significantly higher alpha than softer Y axis");
  }
  // Test Per-Dab Scatter and Stroke Angle Follow
  {
    kreart::brush::RoundBrush scatter_brush;
    kreart::brush::Settings ss;
    ss.size = 20.0; // radius = 10.0
    ss.scatter = 0.5; // scatter max offset = 5.0
    ss.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    scatter_brush.set_settings(ss);

    std::vector<kreart::brush::BrushDab> dabs;
    scatter_brush.begin_dabs({100.0, 100.0}, 1.0, 0.0, 0.0, &dabs);
    scatter_brush.generate_dabs({150.0, 100.0}, 1.0, 0.0, 0.0, dabs);

    check(dabs.size() > 2, "dabs generated for scatter test");
    bool has_nonzero_offset = false;
    bool all_bounded = true;
    for (const auto &d : dabs) {
      const float diff_y = std::abs(d.y - 100.0f);
      if (diff_y > 0.01f) {
        has_nonzero_offset = true;
      }
      if (diff_y > 5.001f) {
        all_bounded = false;
      }
    }
    check(has_nonzero_offset, "scatter produces per-dab position jitter");
    check(all_bounded, "scatter offsets are strictly bounded by radius * scatter");

    // Angle follow
    kreart::brush::RoundBrush angle_brush;
    kreart::brush::Settings as;
    as.size = 20.0;
    as.angle = 0.0;
    as.angle_follow = true;
    as.angle_follow_weight = 1.0;
    as.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    angle_brush.set_settings(as);

    std::vector<kreart::brush::BrushDab> angle_dabs;
    angle_brush.begin_dabs({50.0, 50.0}, 1.0, 0.0, 0.0, &angle_dabs);
    angle_brush.generate_dabs({50.0, 100.0}, 1.0, 0.0, 0.0, angle_dabs); // downward stroke: dy = +50, tangent = pi/2
    check(angle_dabs.size() > 2, "dabs generated for angle follow test");
    const float expected_angle = static_cast<float>(M_PI / 2.0);
    check(std::abs(angle_dabs.back().angle - expected_angle) < 0.01f,
          "angle follow aligns dab angle to stroke tangent pi/2");
  }
  // Test Flow / Opacity Separation and Stroke Buffer Isolation
  {
    kreart::brush::RoundBrush flow_brush;
    kreart::brush::Settings s;
    s.size = 20.0;
    s.opacity = 0.5; // Stroke opacity capped at 50%
    s.flow = 1.0;
    s.color = Rgba8{255, 0, 0, 255};
    s.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    flow_brush.set_settings(s);

    TiledImage img(100, 100);
    flow_brush.begin(img, {50.0, 50.0}, 1.0);
    // Stamp multiple overlapping dabs across the center
    for (int i = 0; i < 15; ++i) {
      flow_brush.move_to(img, {50.0, 50.0}, 1.0);
      flow_brush.move_to(img, {52.0, 50.0}, 1.0);
    }
    flow_brush.end();

    std::uint8_t max_alpha = 0;
    for (int y = 40; y <= 60; ++y) {
      for (int x = 40; x <= 60; ++x) {
        max_alpha = std::max(max_alpha, img.pixel(x, y).a);
      }
    }
    check(max_alpha > 0, "stroke painted onto image");
    check(max_alpha <= 128, "overlapping dabs do not exceed stroke opacity (0.5 * 255 = 127/128)");

    // Test GPU dabs generation has flow = 1.0
    std::vector<kreart::brush::BrushDab> dabs;
    flow_brush.begin_dabs({50.0, 50.0}, 1.0, 0.0, 0.0, &dabs);
    check(!dabs.empty(), "dab generated for GPU flow check");
    check(dabs[0].flow > 0.95f, "GPU dab flow reflects settings.flow = 1.0");
  }
  // Test SensorCurve multi-parameter Dynamics Map evaluation
  {
    kreart::brush::RoundBrush dyn_brush;
    kreart::brush::Settings ds;
    ds.size = 40.0;
    ds.tilt_enabled = false; // isolate size response without geometry tilt scaling
    ds.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    ds.size_dynamics.parse("0.0,0.2;1.0,0.8");
    ds.size_dynamics.set_sensor_type(kreart::brush::SensorType::TILT);
    dyn_brush.set_settings(ds);

    std::vector<kreart::brush::BrushDab> dabs_zero_tilt;
    dyn_brush.begin_dabs({50.0, 50.0}, 1.0, 0.0, 0.0, &dabs_zero_tilt);
    check(!dabs_zero_tilt.empty(), "dab generated for zero tilt dynamics");
    // Expected radius: 40 * 0.5 * (0.15 + 0.85 * 0.2) = 6.4
    check(std::abs(dabs_zero_tilt[0].major_radius - 6.4f) < 0.05f,
          "size dynamics curve routes zero tilt to 6.4 radius");

    std::vector<kreart::brush::BrushDab> dabs_full_tilt;
    dyn_brush.begin_dabs({50.0, 50.0}, 1.0, 1.0, 0.0, &dabs_full_tilt);
    check(!dabs_full_tilt.empty(), "dab generated for full tilt dynamics");
    // Expected radius: 40 * 0.5 * (0.15 + 0.85 * 0.8) = 16.6
    check(std::abs(dabs_full_tilt[0].major_radius - 16.6f) < 0.05f,
          "size dynamics curve routes full tilt to 16.6 radius");
  }
  // Test Brush-Level CompositeOp (Multiply blend)
  {
    kreart::brush::RoundBrush comp_brush;
    kreart::brush::Settings cs;
    cs.size = 20.0;
    cs.opacity = 1.0;
    cs.flow = 1.0;
    cs.composite_op = "multiply";
    cs.color = Rgba8{100, 100, 100, 255};
    cs.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    comp_brush.set_settings(cs);

    TiledImage img(100, 100);
    img.fill(Rgba8{200, 200, 200, 255});

    comp_brush.begin(img, {50.0, 50.0}, 1.0);
    comp_brush.end();

    const auto center_px = img.pixel(50, 50);
    // 200 * 100 / 255 = ~78
    check(center_px.r < 100 && center_px.g < 100 && center_px.b < 100,
          "multiply composite_op darkens underlying canvas pixels (expected ~78)");
    check(std::abs(static_cast<int>(center_px.r) - 78) <= 2,
          "multiply blend arithmetic matches (200 * 100 / 255 = 78)");
  }
  // Test Delayed Stroke Catch-up Smoothing
  {
    kreart::brush::RoundBrush del_brush;
    kreart::brush::Settings ds;
    ds.size = 20.0;
    ds.smoothing_mode = kreart::brush::SmoothingMode::DELAYED_STROKE;
    ds.smoothing_distance = 30.0;
    del_brush.set_settings(ds);

    std::vector<kreart::brush::BrushDab> dabs;
    del_brush.begin_dabs({10.0, 10.0}, 1.0, 0.0, 0.0, &dabs);
    del_brush.generate_dabs({100.0, 10.0}, 1.0, 0.0, 0.0, dabs);

    check(del_brush.smoothed_point().x < 90.0,
          "delayed stroke lags behind sharp position step (lagging behind 100.0)");

    del_brush.end_dabs(dabs);
    check(del_brush.smoothed_point().x > 95.0,
          "delayed stroke tail catch-up moves towards final coordinate");
  }
  // Test Smudge SMEAR Mode and Reservoir Decay
  {
    Document smear_doc(120, 80);
    constexpr Rgba8 green{0, 200, 50, 255};
    for (int y = 20; y <= 40; ++y) {
      for (int x = 10; x <= 30; ++x) {
        smear_doc.image().set_pixel(x, y, green);
      }
    }

    kreart::brush::RoundBrush smear_brush;
    kreart::brush::Settings ss;
    ss.size = 16.0;
    ss.mode = kreart::brush::Mode::SMUDGE;
    ss.smudge_mode = kreart::brush::SmudgeMode::SMEAR;
    ss.smudge_rate = 0.85;
    ss.smoothing_mode = kreart::brush::SmoothingMode::NONE;
    smear_brush.set_settings(ss);

    smear_brush.begin(smear_doc.image(), {25.0, 30.0}, 1.0);
    smear_brush.move_to(smear_doc.image(), {45.0, 30.0}, 1.0);
    smear_brush.end(&smear_doc.image());

    const auto smeared_px = smear_doc.image().pixel(45, 30);
    std::fprintf(stderr, "DEBUG: smeared_px at (45,30): r=%d, g=%d, b=%d, a=%d\n",
                 (int)smeared_px.r, (int)smeared_px.g, (int)smeared_px.b, (int)smeared_px.a);
    check(smeared_px.a > 0 && smeared_px.g > 100,
          "smear mode drags green pixels from patch across canvas onto (45, 30)");

    // Test reservoir decay when dragging far into transparent region without fresh paint
    smear_brush.begin(smear_doc.image(), {45.0, 30.0}, 1.0);
    smear_brush.move_to(smear_doc.image(), {70.0, 30.0}, 1.0);
    smear_brush.move_to(smear_doc.image(), {100.0, 30.0}, 1.0);
    smear_brush.end(&smear_doc.image());

    const auto near_px = smear_doc.image().pixel(55, 30);
    const auto far_px = smear_doc.image().pixel(98, 30);
    check(near_px.a > far_px.a,
          "reservoir decay causes paint opacity to deplete as stroke drags across empty canvas");
  }
  // Color Burn must actually blend (the layer panel used to write "burn",
  // which blend_layer_pixel did not implement).
  {
    Document burn_doc(32, 32);
    burn_doc.layer(0).image().fill(Rgba8{128, 128, 128, 255});
    const std::size_t burn_top = burn_doc.add_layer("Burn");
    burn_doc.layer(burn_top).image().fill(Rgba8{128, 128, 128, 255});
    burn_doc.set_layer_composite_op(burn_top, "color_burn");
    check(burn_doc.composite_image().pixel(10, 10).r < 20,
          "color_burn layer composite darkens identical mid-grey to near black");

    // The composite revision must advance on mutation and stay stable on reads.
    const std::uint64_t rev0 = burn_doc.composite_revision();
    burn_doc.set_layer_opacity(burn_top, 0.5);
    const std::uint64_t rev1 = burn_doc.composite_revision();
    check(rev1 > rev0, "composite_revision advances when layer opacity changes");
    burn_doc.composite_image();
    burn_doc.layer(burn_top).name();
    check(burn_doc.composite_revision() == rev1,
          "composite_revision stays stable across read-only composite access");

    // The CPU composite path clears dirty_ but must not clear the GPU set,
    // otherwise the next paintGL uploads nothing and the GPU shows stale pixels.
    Document dual(128, 128);
    dual.image().set_pixel(5, 5, Rgba8{1, 2, 3, 255});
    check(!dual.image().gpu_dirty_tiles().empty(), "painting marks the GPU dirty set");
    dual.composite_image();
    check(!dual.image().gpu_dirty_tiles().empty(),
          "CPU composite does not clear the GPU dirty set");
  }

  // Non-rectangular selection shapes: ellipse and flood_select populate contours and masks
  {
    kreart::core::SelectionMask sel(64, 64);
    sel.set_ellipse(10, 10, 44, 44);
    check(sel.has_selection(), "Ellipse selection is active");
    check(sel.is_selected(32, 32), "Center of ellipse is selected");
    check(!sel.is_selected(10, 10), "Top-left corner of bounding box is not selected");
    check(!sel.is_selected(53, 10), "Top-right corner of bounding box is not selected");
    check(!sel.is_selected(10, 53), "Bottom-left corner of bounding box is not selected");
    check(sel.lasso_points().size() >= 3, "Ellipse selection populates boundary contour points");

    Document wand_doc(64, 64);
    wand_doc.layer(0).image().fill(Rgba8{0, 0, 0, 0});
    for (int y = 22; y <= 42; ++y) {
      for (int x = 22; x <= 42; ++x) {
        if (std::hypot(x - 32, y - 32) <= 10.0) {
          wand_doc.layer(0).image().set_pixel(x, y, Rgba8{255, 0, 0, 255});
        }
      }
    }
    wand_doc.flood_select(32, 32, 20, true, kreart::core::SelectionCombineMode::REPLACE);
    check(wand_doc.selection().has_selection(), "flood_select created active selection");
    check(wand_doc.selection().is_selected(32, 32), "Center of blob is selected by magic wand");
    check(!wand_doc.selection().is_selected(5, 5), "Background is not selected by magic wand");
    check(wand_doc.selection().bounds().width > 0 && wand_doc.selection().bounds().height > 0,
          "Magic wand selection bounds are non-empty");
  }
  {
    // Step 1: ProjectTracker tests
    kreart::core::ProjectTracker tracker;
    tracker.reset(800, 600);
    check(!tracker.session_uuid().empty(), "Tracker generates non-empty UUID");
    check(tracker.doc_width() == 800 && tracker.doc_height() == 600, "Tracker dimensions match");
    check(tracker.total_strokes() == 0, "Initial strokes is 0");
    check(tracker.active_draw_time_ms() == 0, "Initial active draw time is 0");

    std::vector<kreart::core::DocumentChangeEvent> captured_events;
    tracker.set_change_listener([&](const kreart::core::DocumentChangeEvent &evt) {
      captured_events.push_back(evt);
    });

    kreart::core::TrackedBrushSettings brush;
    brush.size = 25.0;
    brush.opacity = 0.8;
    brush.color = Rgba8{120, 50, 200, 255};

    tracker.begin_stroke(0, brush, 1000);
    check(tracker.in_stroke(), "Tracker reports in_stroke true");
    tracker.append_sample({10.0, 15.0, 0.5, 0.0, 0.0, 1010});
    tracker.append_sample({12.0, 18.0, 0.7, 0.0, 0.0, 1020});
    tracker.append_sample({15.0, 22.0, 0.9, 0.0, 0.0, 1030});

    std::vector<std::uint64_t> tiles = {TiledImage::key_for(0, 0)};
    tracker.commit_stroke(tiles, 1050);

    check(!tracker.in_stroke(), "Tracker reports in_stroke false after commit");
    check(tracker.total_strokes() == 1, "total_strokes incremented to 1");
    check(tracker.active_draw_time_ms() == 50, "active_draw_time_ms correctly tracked (50ms)");
    check(captured_events.size() == 1, "One stroke event captured");
    check(captured_events[0].type == kreart::core::DocumentChangeType::STROKE_COMMITTED, "Event type is STROKE_COMMITTED");
    check(captured_events[0].stroke_points.size() == 3, "Captured 3 stroke points");
    check(captured_events[0].modified_tile_keys.size() == 1, "Captured 1 modified tile key");

    // Cancel stroke test
    tracker.begin_stroke(0, brush, 2000);
    tracker.append_sample({20.0, 30.0, 0.5, 0.0, 0.0, 2010});
    tracker.cancel_stroke();
    check(!tracker.in_stroke(), "Tracker reports in_stroke false after cancel");
    check(tracker.total_strokes() == 1, "total_strokes not incremented on cancel");
    check(captured_events.size() == 1, "No event captured on cancel");

    // Generic document change test
    tracker.record_document_change(kreart::core::DocumentChangeType::LAYER_ADDED, 1, {}, 3000);
    check(captured_events.size() == 2, "Document change event captured");
    check(captured_events[1].type == kreart::core::DocumentChangeType::LAYER_ADDED, "Captured LAYER_ADDED type");
  }
  {
    // Step 2: WalJournal tests
    using namespace kreart::io;
    std::string test_uuid = "12345678-abcd-4ef0-9123-456789abcdef";
    std::uint8_t uuid_bytes[16];
    check(WalJournal::uuid_to_bytes(test_uuid, uuid_bytes), "UUID conversion to bytes succeeded");
    std::string recovered_uuid = WalJournal::bytes_to_uuid(uuid_bytes);
    check(recovered_uuid == test_uuid, "UUID round-trip matches original");

    // CRC32 check
    const char *test_data = "123456789";
    std::uint32_t crc = WalJournal::calculate_crc32(reinterpret_cast<const std::uint8_t*>(test_data), 9);
    // Standard check value for "123456789" with IEEE 802.3 CRC-32 is 0xCBF43926
    check(crc == 0xCBF43926U, "IEEE 802.3 CRC32 of '123456789' matches standard 0xCBF43926");

    // Test writing a WAL file
    std::filesystem::path wal_test_file = std::filesystem::temp_directory_path() / "test_journal.wal";
    std::error_code ec;
    std::filesystem::remove(wal_test_file, ec);

    {
      WalJournal journal;
      check(journal.open(wal_test_file, 1920, 1080, test_uuid), "WalJournal opens successfully");
      check(journal.is_open(), "WalJournal is_open true");

      // Record stroke with a tile
      kreart::core::DocumentChangeEvent evt;
      evt.type = kreart::core::DocumentChangeType::STROKE_COMMITTED;
      evt.layer_id = 0;
      evt.timestamp_ms = 12345;
      evt.brush_settings.size = 12.0;
      evt.brush_settings.color = Rgba8{200, 100, 50, 255};
      evt.stroke_points.push_back({100.0, 200.0, 1.0, 0.0, 0.0, 12345});

      CapturedTile cap;
      cap.tx = 0;
      cap.ty = 0;
      cap.tile = std::make_shared<kreart::core::Tile>();
      cap.tile->pixels[0] = Rgba8{200, 100, 50, 255};

      journal.record_stroke(evt, {cap});
      journal.flush_sync();

      // Checkpoint
      journal.checkpoint(0xAABBCCDD);
      journal.close();
      check(!journal.is_open(), "WalJournal closed successfully");
    }

    // Verify file content
    FILE *f = std::fopen(wal_test_file.string().c_str(), "rb");
    check(f != nullptr, "WAL file exists on disk");
    if (f) {
      WalHeader header;
      check(std::fread(&header, 1, sizeof(header), f) == sizeof(header), "Read WAL header (32 bytes)");
      check(header.magic == kWALMagic, "Header magic is KWAL");
      check(header.version == kWALVersion, "Header version is 1");
      check(header.flags == 1, "Header flags is 1 (checkpointed)");
      check(header.doc_width == 1920 && header.doc_height == 1080, "Header dimensions match 1920x1080");
      check(WalJournal::bytes_to_uuid(header.session_id) == test_uuid, "Header session_id matches UUID");
      std::fclose(f);
    }
    std::filesystem::remove(wal_test_file, ec);
  }
  {
    // Step 3: Broken project recovery and resilient loading tests
    using namespace kreart::io;
    std::filesystem::path recovery_test_wal = std::filesystem::temp_directory_path() / "corrupt_test.wal";
    std::error_code ec;
    std::filesystem::remove(recovery_test_wal, ec);

    std::string session_id = "aabbccdd-1122-3344-5566-778899aabbcc";
    {
      WalJournal journal;
      check(journal.open(recovery_test_wal, 800, 600, session_id), "Created recovery test journal");

      // Stroke 1 on layer 0
      kreart::core::DocumentChangeEvent s1;
      s1.type = kreart::core::DocumentChangeType::STROKE_COMMITTED;
      s1.layer_id = 0;
      s1.timestamp_ms = 1000;
      CapturedTile t0;
      t0.tx = 0;
      t0.ty = 0;
      t0.tile = std::make_shared<kreart::core::Tile>();
      t0.tile->pixels[0] = Rgba8{255, 0, 0, 255}; // Red pixel at (0, 0)
      journal.record_stroke(s1, {t0});

      // Layer mutation: add layer 1
      journal.record_layer_mutation(kreart::core::DocumentChangeType::LAYER_ADDED, 1, "Layer 2", 1500);

      // Stroke 2 on layer 1
      kreart::core::DocumentChangeEvent s2;
      s2.type = kreart::core::DocumentChangeType::STROKE_COMMITTED;
      s2.layer_id = 1;
      s2.timestamp_ms = 2000;
      CapturedTile t1;
      t1.tx = 1;
      t1.ty = 0;
      t1.tile = std::make_shared<kreart::core::Tile>();
      t1.tile->pixels[0] = Rgba8{0, 255, 0, 255}; // Green pixel at (128, 0)
      journal.record_stroke(s2, {t1});

      journal.flush_sync();
      journal.close();
    }

    // Intentionally corrupt/truncate tail: append partial/corrupt bytes
    {
      FILE *f = std::fopen(recovery_test_wal.string().c_str(), "ab");
      check(f != nullptr, "Reopened WAL to append corrupt tail");
      if (f) {
        const char garbage[] = "CORRUPT_TAIL_SIMULATING_MID_WRITE_CRASH";
        std::fwrite(garbage, 1, sizeof(garbage), f);
        std::fclose(f);
      }
    }

    // Inspect WAL
    WalRecoveryStats stats;
    std::string inspect_err;
    check(inspect_wal(recovery_test_wal, stats, inspect_err), "inspect_wal succeeded");
    check(stats.doc_width == 800 && stats.doc_height == 600, "inspect_wal got dimensions 800x600");
    check(stats.stroke_events_replayed == 2, "inspect_wal detected 2 strokes");
    check(stats.tile_diffs_replayed == 2, "inspect_wal detected 2 tile diffs");
    check(stats.mutations_applied == 1, "inspect_wal detected 1 layer mutation");
    check(stats.reached_eof_or_corrupt_tail, "inspect_wal detected corrupt tail at crash point");

    // Recover into blank document
    kreart::core::Document recovered_doc(100, 100);
    std::string recovery_report;
    check(recover_document_from_wal(recovery_test_wal, recovered_doc, recovery_report, &stats),
          "recover_document_from_wal succeeded despite corrupted tail");
    check(recovered_doc.width() == 800 && recovered_doc.height() == 600,
          "Recovered document dimensions match 800x600");
    check(recovered_doc.layer_count() >= 2, "Recovered document has at least 2 layers");
    check(recovered_doc.layer(0).image().pixel(0, 0).r == 255,
          "Layer 0 recovered red pixel at (0, 0)");
    check(recovered_doc.layer(1).image().pixel(128, 0).g == 255,
          "Layer 1 recovered green pixel at (128, 0)");

    std::filesystem::remove(recovery_test_wal, ec);
  }
  {
    // Step 4: TimelapseRecorder tests
    using namespace kreart::io;
    std::string tl_uuid = "timelapse-test-uuid-42";
    std::filesystem::path session_dir = TimelapseRecorder::get_session_dir_for(tl_uuid);
    std::error_code ec;
    std::filesystem::remove_all(session_dir, ec);

    TimelapseRecorder recorder;
    check(recorder.init(tl_uuid, 800), "TimelapseRecorder initialized");
    check(recorder.frame_count() == 0, "Initial frame count is 0");

    // Create test pixel buffer (100x100)
    std::vector<kreart::core::Rgba8> px(100 * 100, kreart::core::Rgba8{100, 150, 200, 255});
    recorder.capture_frame_async(px, 100, 100, true);
    recorder.flush_pending_frames();
    check(recorder.frame_count() == 1, "Frame count incremented to 1");

    std::filesystem::path f0 = recorder.frame_path_for_index(0);
    check(std::filesystem::exists(f0, ec), "frame_000000.jpg exists on disk");
    check(std::filesystem::file_size(f0, ec) > 0, "frame_000000.jpg is not empty");

    // Rate limiting check: without force, instant consecutive frame should be ignored
    recorder.capture_frame_async(px, 100, 100, false);
    recorder.flush_pending_frames();
    check(recorder.frame_count() == 1, "Sub-100ms capture debounced (still 1)");

    // CPU fallback capture test
    kreart::core::Document doc(400, 300);
    doc.layer(0).image().fill(kreart::core::Rgba8{255, 50, 50, 255});
    doc.mark_composite_dirty();
    doc.composite_image();
    recorder.capture_document_cpu(doc, 400, true);
    recorder.flush_pending_frames();
    check(recorder.frame_count() == 2, "CPU capture added frame 2");
    std::filesystem::path f1 = recorder.frame_path_for_index(1);
    check(std::filesystem::exists(f1, ec), "frame_000001.jpg exists on disk");

    // Metadata persistence test
    TimelapseMetadata meta;
    check(TimelapseRecorder::load_metadata(session_dir, meta), "load_metadata succeeded");
    check(meta.session_uuid == tl_uuid, "Metadata session_uuid matches");
    check(meta.frame_count == 2, "Metadata frame_count is 2");

    recorder.stop();
    std::filesystem::remove_all(session_dir, ec);
  }
  {
    // Step 5: Hybrid KRA Packaging for Timelapse
    using namespace kreart::io;
    std::string test_session = "packaging-session-99";
    std::filesystem::path session_dir = TimelapseRecorder::get_session_dir_for(test_session);
    std::error_code ec;
    std::filesystem::remove_all(session_dir, ec);

    // Record 2 frames into session
    TimelapseRecorder recorder;
    recorder.init(test_session, 400);
    std::vector<kreart::core::Rgba8> px(50 * 50, kreart::core::Rgba8{50, 100, 150, 255});
    recorder.capture_frame_async(px, 50, 50, true);
    recorder.capture_frame_async(px, 50, 50, true);
    recorder.flush_pending_frames();
    recorder.stop();

    // Save document to KRA with timelapse
    kreart::core::Document doc(200, 200);
    doc.layer(0).image().fill(kreart::core::Rgba8{200, 200, 200, 255});
    doc.mark_composite_dirty();
    doc.composite_image();

    std::filesystem::path kra_path = std::filesystem::temp_directory_path() / "hybrid_timelapse_test.kra";
    std::filesystem::remove(kra_path, ec);
    std::string kra_err;
    check(write_kra(kra_path, doc, kra_err, test_session), "write_kra with timelapse succeeded");
    check(std::filesystem::exists(kra_path, ec), "KRA file created on disk");

    // Now load the KRA file into a new session UUID
    std::string import_session = "imported-session-88";
    std::filesystem::path import_dir = TimelapseRecorder::get_session_dir_for(import_session);
    std::filesystem::remove_all(import_dir, ec);

    kreart::core::Document loaded_doc(10, 10);
    check(read_kra(kra_path, loaded_doc, kra_err, import_session), "read_kra extracted hybrid KRA");
    check(loaded_doc.width() == 200 && loaded_doc.height() == 200, "Loaded document dimensions match");

    // Verify extracted timelapse in import_dir
    TimelapseMetadata imported_meta;
    check(TimelapseRecorder::load_metadata(import_dir, imported_meta), "Imported timelapse metadata found");
    check(imported_meta.frame_count == 2, "Imported timelapse frame_count is 2");
    check(std::filesystem::exists(import_dir / "frame_000000.jpg", ec), "Extracted frame_000000.jpg exists");
    check(std::filesystem::exists(import_dir / "frame_000001.jpg", ec), "Extracted frame_000001.jpg exists");

    std::filesystem::remove(kra_path, ec);
    std::filesystem::remove_all(session_dir, ec);
    std::filesystem::remove_all(import_dir, ec);
  }
  {
    // Step 6: In-App Timelapse Replay & Universal MP4 Video Exporter tests
    using namespace kreart::io;
    check(is_ffmpeg_available(), "is_ffmpeg_available detected system ffmpeg");

    // Prepare a test directory with 5 frames
    std::string test_mp4_session = "mp4-export-session-123";
    std::filesystem::path session_dir = TimelapseRecorder::get_session_dir_for(test_mp4_session);
    std::error_code ec;
    std::filesystem::remove_all(session_dir, ec);

    TimelapseRecorder recorder;
    recorder.init(test_mp4_session, 160);
    for (int i = 0; i < 5; ++i) {
      std::vector<kreart::core::Rgba8> px(160 * 160, kreart::core::Rgba8{static_cast<uint8_t>(i * 50), 120, 200, 255});
      recorder.capture_frame_async(px, 160, 160, true);
    }
    recorder.flush_pending_frames();
    recorder.stop();

    // 1. Test Hardware / FFmpeg Export
    std::filesystem::path out_ffmpeg_mp4 = std::filesystem::temp_directory_path() / "test_ffmpeg.mp4";
    std::filesystem::remove(out_ffmpeg_mp4, ec);

    std::string err_msg;
    int last_pct = 0;
    bool ff_ok = export_timelapse_to_mp4(session_dir, 5, out_ffmpeg_mp4, 30, true, [&](int pct) {
      last_pct = pct;
    }, &err_msg);

    check(ff_ok, "export_timelapse_to_mp4 with FFmpeg succeeded");
    check(std::filesystem::exists(out_ffmpeg_mp4, ec), "FFmpeg output MP4 exists");
    check(std::filesystem::file_size(out_ffmpeg_mp4, ec) > 0, "FFmpeg output MP4 size > 0");

    // 2. Test Universal Embedded Fallback Export (prefer_ffmpeg = false)
    std::filesystem::path out_embedded_mp4 = std::filesystem::temp_directory_path() / "test_embedded.mp4";
    std::filesystem::remove(out_embedded_mp4, ec);

    bool emb_ok = export_timelapse_to_mp4(session_dir, 5, out_embedded_mp4, 30, false, nullptr, &err_msg);
    check(emb_ok, "export_timelapse_to_mp4 with embedded fallback encoder succeeded");
    check(std::filesystem::exists(out_embedded_mp4, ec), "Embedded output MP4 exists");
    check(std::filesystem::file_size(out_embedded_mp4, ec) > 0, "Embedded output MP4 size > 0");

    std::filesystem::remove(out_ffmpeg_mp4, ec);
    std::filesystem::remove(out_embedded_mp4, ec);
    std::filesystem::remove_all(session_dir, ec);
  }
  {
    // Step 7: 3-Layer Crash Recovery & Truncated File Reconstruction Test
    using namespace kreart::io;
    std::error_code ec;
    std::filesystem::path test_kra = std::filesystem::temp_directory_path() / "test_crash_recovery.kra";
    std::filesystem::path test_wal = WalJournal::default_journal_path_for(test_kra);
    std::filesystem::remove(test_kra, ec);
    std::filesystem::remove(test_wal, ec);

    std::string session_id = "crash-recovery-3layer-session";
    {
      WalJournal journal;
      check(journal.open(test_wal, 300, 200, session_id), "Created companion WAL for crash recovery test");

      // Layer 0: Blue pixel
      CapturedTile cap0;
      cap0.tx = 0; cap0.ty = 0;
      cap0.tile = std::make_shared<kreart::core::Tile>();
      cap0.tile->pixels[0] = Rgba8{0, 0, 255, 255};
      journal.record_tile_diff(0, {cap0}, 1000);

      // Mutation: add Layer 1
      journal.record_layer_mutation(kreart::core::DocumentChangeType::LAYER_ADDED, 1, "Layer 2", 1100);
      // Layer 1: Red pixel
      CapturedTile cap1;
      cap1.tx = 0; cap1.ty = 0;
      cap1.tile = std::make_shared<kreart::core::Tile>();
      cap1.tile->pixels[1] = Rgba8{255, 0, 0, 255};
      journal.record_tile_diff(1, {cap1}, 1200);

      // Mutation: add Layer 2
      journal.record_layer_mutation(kreart::core::DocumentChangeType::LAYER_ADDED, 2, "Layer 3", 1300);
      // Layer 2: Green pixel
      CapturedTile cap2;
      cap2.tx = 0; cap2.ty = 0;
      cap2.tile = std::make_shared<kreart::core::Tile>();
      cap2.tile->pixels[2] = Rgba8{0, 255, 0, 255};
      journal.record_tile_diff(2, {cap2}, 1400);

      journal.flush_sync();
      journal.close();
    }

    // Simulate sudden power loss or process kill mid-save: truncate KRA to 0 bytes
    {
      FILE *f = std::fopen(test_kra.string().c_str(), "wb");
      check(f != nullptr, "Created 0-byte truncated KRA simulating crash");
      if (f) std::fclose(f);
    }
    check(std::filesystem::file_size(test_kra, ec) == 0, "Corrupted KRA is 0 bytes");

    // Verify recovery discovery and reconstruction
    std::filesystem::path discovered_wal;
    check(has_uncommitted_wal(test_kra, discovered_wal), "has_uncommitted_wal detected companion journal");
    check(discovered_wal == test_wal, "Discovered WAL path matches companion");

    kreart::core::Document reconstructed(1, 1);
    std::string report;
    WalRecoveryStats rec_stats;
    check(recover_document_from_wal(discovered_wal, reconstructed, report, &rec_stats),
          "Reconstructed damaged project from journal");
    check(reconstructed.width() == 300 && reconstructed.height() == 200, "Reconstructed dimensions match 300x200");
    check(reconstructed.layer_count() >= 3, "All 3 layers reconstructed");
    check(reconstructed.layer(0).image().pixel(0, 0).b == 255, "Layer 0 blue pixel intact");
    check(reconstructed.layer(1).image().pixel(1, 0).r == 255, "Layer 1 red pixel intact");
    check(reconstructed.layer(2).image().pixel(2, 0).g == 255, "Layer 2 green pixel intact");

    std::filesystem::remove(test_kra, ec);
    std::filesystem::remove(test_wal, ec);
  }

  if (failures == 0) {
    std::puts("kreart core tests: all checks passed");
    return 0;
  }
  std::fprintf(stderr, "kreart core tests: %d failure(s)\n", failures);
  return 1;
}
