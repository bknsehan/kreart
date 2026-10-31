#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <fstream>
#include <QByteArray>

#include "kreart/resources/preset.h"
#include "kreart/resources/resource_database.h"
#include "kreart/brush/brush_tip.h"
#include "kreart/brush/sensor_curve.h"

#ifndef KREART_TEST_FIXTURE_DIR
#error "KREART_TEST_FIXTURE_DIR must point at tests/fixtures"
#endif

namespace {

int failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool close_to(double actual, double expected, double tolerance = 1e-9) {
  return std::abs(actual - expected) <= tolerance;
}

const char *kTestXml = R"XML(<Preset name="b)_Basic-5_Size" paintopid="paintbrush">
 <param name="CompositeOp" type="string"><![CDATA[normal]]></param>
 <param name="FlowValue" type="string"><![CDATA[0.5]]></param>
 <param name="OpacityValue" type="string"><![CDATA[0.8]]></param>
 <param name="PressureSize" type="string"><![CDATA[true]]></param>
 <param name="Unknown/FutureField" type="string"><![CDATA[kept]]></param>
 <param name="brush_definition" type="string"><![CDATA[
   <Brush type="auto_brush" spacing="0.2" useAutoSpacing="1" autoSpacingCoeff="0.8">
     <MaskGenerator type="circle" diameter="42" hfade="1" vfade="0.5" ratio="1"
                    spikes="2" antialiasEdges="1"/>
   </Brush>
 ]]></param>
</Preset>)XML";

} // namespace

int main() {
  using kreart::resources::Preset;
  using kreart::resources::ResourceDatabase;

  Preset inline_preset;
  std::string error;
  check(kreart::resources::parse_preset_xml(kTestXml, inline_preset, error),
        "inline preset XML parses");
  check(inline_preset.name == "b)_Basic-5_Size", "preset name is preserved");
  check(inline_preset.display_name == "b) Basic-5 Size", "display name is normalized");
  check(inline_preset.paintop_id == "paintbrush", "paint operation id is parsed");
  check(inline_preset.attributes.find("paintopid") != inline_preset.attributes.end(),
        "root preset attributes are preserved");
  check(inline_preset.value("Unknown/FutureField") == "kept",
        "unknown preset parameters are preserved");
  check(close_to(inline_preset.number("FlowValue", 0.0), 0.5), "numeric parameters are parsed");
  check(inline_preset.boolean("PressureSize", false), "boolean parameters are parsed");
  check(!inline_preset.is_eraser(), "normal presets are not erasers");
  check(inline_preset.brush.has_value(), "brush definition is parsed");
  if (inline_preset.brush) {
    check(inline_preset.brush->type == "auto_brush", "brush type is parsed");
    check(inline_preset.brush->bool_attribute("useAutoSpacing", false),
          "brush boolean attributes are parsed");
    check(close_to(inline_preset.brush->number_attribute("autoSpacingCoeff", 0.0), 0.8),
          "brush numeric attributes are parsed");
    check(inline_preset.brush->mask.has_value(), "mask generator is parsed");
  }
  if (inline_preset.brush && inline_preset.brush->mask) {
    check(close_to(inline_preset.brush->mask->diameter, 42.0), "mask diameter is parsed");
    check(close_to(inline_preset.brush->mask->horizontal_fade, 1.0), "horizontal fade is parsed");
    check(close_to(inline_preset.brush->mask->vertical_fade, 0.5), "vertical fade is parsed");
    check(inline_preset.brush->mask->antialias_edges, "mask antialiasing is parsed");
  }

  kreart::brush::Settings base;
  base.size = 99.0;
  base.opacity = 0.9;
  base.hardness = 0.2;
  base.spacing = 0.11;
  base.tilt_enabled = false;
  base.color = {255, 0, 0, 255};
  const kreart::brush::Settings mapped = inline_preset.brush_settings(base);
  check(close_to(mapped.size, 42.0), "preset diameter maps to brush size");
  check(close_to(mapped.opacity, 0.8), "preset opacity maps to brush opacity");
  check(close_to(mapped.flow, 0.5), "preset flow maps to brush flow");
  check(mapped.composite_op == "normal", "preset composite_op maps to brush composite_op");
  check(close_to(mapped.hardness, 0.5), "preset mask fade maps to brush hardness");
  check(close_to(mapped.spacing, 0.2), "preset spacing maps to brush spacing");
  check(!mapped.tilt_enabled, "preset mapping preserves the selected tilt setting");
  check(mapped.color.r == 255 && mapped.color.g == 0, "preset mapping preserves brush color");

  const std::filesystem::path fixtures(KREART_TEST_FIXTURE_DIR);
  const std::filesystem::path preset_dir = fixtures / "presets";

  Preset eraser;
  check(kreart::resources::load_preset(preset_dir / "eraser.kpp", eraser, error),
        "PNG .kpp preset loads");
  check(eraser.format_version == "2.2", "PNG preset version chunk is read");
  check(eraser.is_eraser(), "PNG eraser composite operation is recognized");
  check(eraser.display_name == "a) Eraser Circle", "PNG preset display name is normalized");
  if (eraser.brush && eraser.brush->mask) {
    check(close_to(eraser.brush->mask->diameter, 20.0), "PNG preset brush mask is read");
  }

  Preset compressed;
  check(kreart::resources::load_preset(preset_dir / "compressed.kpp", compressed, error),
        "gzip-compressed XML preset loads");
  check(compressed.name == "Compressed_Test", "gzip preset name is read");
  check(compressed.format_version.empty(), "gzip preset has no PNG version");

  Preset plain;
  check(kreart::resources::load_preset(preset_dir / "plain.kpp", plain, error),
        "plain XML preset loads");
  check(plain.name == "Plain_Test", "plain XML preset name is read");

  Preset text_chunk;
  check(kreart::resources::load_preset(preset_dir / "text.kpp", text_chunk, error),
        "uncompressed PNG tEXt preset loads");
  check(text_chunk.name == "Text_Chunk_Test", "tEXt preset name is read");

  Preset itxt;
  check(kreart::resources::load_preset(preset_dir / "itxt.kpp", itxt, error),
        "compressed PNG iTXt preset loads");
  check(itxt.name == "ITXt_Test", "iTXt preset name is read");

  ResourceDatabase database;
  database.add_search_path(preset_dir);
  const kreart::resources::ScanResult scan = database.scan(false);
  check(scan.discovered == 7, "database discovers every .kpp fixture");
  check(scan.loaded == 6, "database keeps valid presets");
  check(scan.failed == 1, "database reports the broken preset");
  check(scan.diagnostics.size() == 1, "database records one parse diagnostic");
  check(database.size() == 6, "database exposes six presets");
  if (database.size() == 6) {
    check(database.presets()[0].display_name == "a) Eraser Circle",
          "database sorts presets by display name");
    check(database.presets()[1].display_name == "b) Basic-5 Size",
          "database keeps the basic preset in order");
    check(database.presets()[2].display_name == "Compressed Test",
          "database includes the gzip preset");
    check(database.presets()[3].display_name == "ITXt Test", "database includes the iTXt preset");
    check(database.presets()[4].display_name == "Plain Test",
          "database includes the plain XML preset");
    check(database.presets()[5].display_name == "Text Chunk Test",
          "database includes the tEXt preset");
  }

  ResourceDatabase override_database;
  override_database.add_search_path(fixtures / "override" / "system");
  override_database.add_search_path(fixtures / "override" / "user");
  const kreart::resources::ScanResult override_scan = override_database.scan(false);
  check(override_scan.discovered == 2, "override scan sees both copies");
  check(override_scan.failed == 0, "override scan has no failures");
  check(override_database.size() == 1, "same filename from a later root replaces the earlier one");
  if (override_database.size() == 1) {
    check(override_database.presets()[0].name == "User_Preset",
          "user preset root has priority over the system root");
    const kreart::brush::Settings override_settings =
        override_database.presets()[0].brush_settings(base);
    check(close_to(override_settings.size, 30.0), "override preset contents are used");
  }

  check(kreart::resources::preset_display_name("j)_WaterC_Spread") == "j) WaterC Spread",
        "display names strip underscore separators");
  check(kreart::resources::preset_display_name("  double__space  ") == "double space",
        "display names collapse repeated separators");

  Preset malformed;
  check(!kreart::resources::parse_preset_xml("<Preset", malformed, error),
        "malformed XML is rejected");
  check(!kreart::resources::load_preset(preset_dir / "broken.kpp", malformed, error),
        "non-preset files are rejected");

  // SensorCurve unit tests
  kreart::brush::SensorCurve curve;
  check(curve.parse("0,0;0.5,0.8;1,1;"), "parse valid curve string");
  check(curve.active(), "curve is active after parsing");
  check(close_to(curve.evaluate(0.0), 0.0), "curve evaluates at 0");
  check(close_to(curve.evaluate(0.25), 0.4), "curve evaluates midpoint segment 1");
  check(close_to(curve.evaluate(0.5), 0.8), "curve evaluates segment junction");
  check(close_to(curve.evaluate(0.75), 0.9), "curve evaluates midpoint segment 2");
  check(close_to(curve.evaluate(1.0), 1.0), "curve evaluates at 1");

  // Curve embedded in XML tag
  kreart::brush::SensorCurve xml_curve;
  check(xml_curve.parse("<curve>0,1;1,0;</curve>"), "parse curve from XML tag");
  check(close_to(xml_curve.evaluate(0.0), 1.0), "inverse curve at 0 is 1");
  check(close_to(xml_curve.evaluate(1.0), 0.0), "inverse curve at 1 is 0");
  check(close_to(xml_curve.evaluate(0.5), 0.5), "inverse curve at 0.5 is 0.5");

  // BrushTip unit tests
  // Construct a minimal 4x4 GBR buffer
  std::vector<std::uint8_t> gbr_buf(28 + 16, 0);
  // Big-endian header: size 28, version 2, width 4, height 4, bpp 1, magic "GIMP", spacing 1
  gbr_buf[3] = 28;
  gbr_buf[7] = 2;
  gbr_buf[11] = 4;
  gbr_buf[15] = 4;
  gbr_buf[19] = 1;
  gbr_buf[20] = 'G'; gbr_buf[21] = 'I'; gbr_buf[22] = 'M'; gbr_buf[23] = 'P';
  gbr_buf[27] = 1;
  // Set center pixels to 255
  gbr_buf[28 + 5] = 255;
  gbr_buf[28 + 6] = 255;
  gbr_buf[28 + 9] = 255;
  gbr_buf[28 + 10] = 255;

  auto tip = kreart::brush::BrushTip::load_gbr(gbr_buf.data(), gbr_buf.size());
  check(tip != nullptr, "load_gbr parses valid GBR buffer");
  check(tip && tip->width() == 4 && tip->height() == 4, "GBR dimensions are 4x4");
  check(tip && tip->sample(0.5, 0.5) > 0.9, "GBR center sample is high");
  check(tip && tip->sample(0.0, 0.0) < 0.1, "GBR corner sample is low");

  // Categories check
  check(!database.categories().empty(), "database categories are populated");
  check(std::find(database.categories().begin(), database.categories().end(), "All") !=
            database.categories().end(),
        "categories include 'All'");
  check(std::find(database.categories().begin(), database.categories().end(), "Erasers") !=
            database.categories().end(),
        "categories include 'Erasers'");
  const auto erasers = database.presets_for_category("Erasers");
  check(!erasers.empty(), "presets_for_category returns eraser presets");

  // Bundle brush tip search test
  auto bundle_tip = database.find_brush_tip("bokey_circle.gbr");
  if (bundle_tip) {
    check(bundle_tip->width() > 0 && bundle_tip->height() > 0,
          "bundle brush tip loaded from installed Krita bundle");
  }
  // Test bundle scanning and extraction
  for (const auto &bundle : ResourceDatabase::standard_bundle_paths(true)) {
    if (std::filesystem::exists(bundle) && bundle.string().find("Krita_4") != std::string::npos) {
      std::error_code ec;
      auto temp_dest = std::filesystem::temp_directory_path(ec) / "kreart_test_bundle_extract";
      std::filesystem::remove_all(temp_dest, ec);
      auto res = ResourceDatabase::unpack_bundle(bundle, temp_dest);
      check(res.success, "unpack_bundle succeeds on installed bundle");
      check(res.presets_imported > 0, "unpack_bundle imports presets from bundle");
      std::filesystem::remove_all(temp_dest, ec);
      break;
    }
  }
  // Pattern loading tests
  {
    std::vector<std::uint8_t> pat_buf(28, 0);
    pat_buf[3] = 24; // hdr_size = 24
    pat_buf[7] = 1;  // version = 1
    pat_buf[11] = 2; // width = 2
    pat_buf[15] = 2; // height = 2
    pat_buf[19] = 1; // bpp = 1
    pat_buf[20] = 't'; pat_buf[21] = 'e'; pat_buf[22] = 's'; pat_buf[23] = '\0';
    pat_buf[24] = 200; pat_buf[25] = 100; pat_buf[26] = 50; pat_buf[27] = 25;

    auto mask = kreart::brush::TextureMask::load_from_memory(pat_buf.data(), pat_buf.size(), "test");
    check(mask != nullptr, "TextureMask::load_from_memory parses valid PAT buffer");
    check(mask && mask->width() == 2 && mask->height() == 2, "TextureMask has 2x2 dimensions");

    // Test loose pattern search in user_resource_directory
    std::error_code ec;
    auto user_pat_dir = ResourceDatabase::user_resource_directory() / "patterns";
    std::filesystem::create_directories(user_pat_dir, ec);
    auto test_pat_file = user_pat_dir / "unit_test_pattern.pat";
    {
      std::ofstream f(test_pat_file, std::ios::binary);
      f.write(reinterpret_cast<const char *>(pat_buf.data()), pat_buf.size());
    }
    auto found_pat = database.find_pattern("unit_test_pattern");
    check(found_pat != nullptr, "ResourceDatabase::find_pattern finds pattern in user directory");
    check(found_pat && found_pat->width() == 2 && found_pat->height() == 2,
          "found pattern has correct dimensions");
    std::filesystem::remove(test_pat_file, ec);

    // Test preset with Texture/Pattern/Name and base64 Pattern
    const char *kPatternXml = R"XML(<Preset name="Pattern_Test" paintopid="paintbrush">
 <param name="Texture/Enabled" type="string"><![CDATA[true]]></param>
 <param name="Texture/Pattern/Name" type="string"><![CDATA[InlinePat]]></param>
 <param name="Texture/Pattern/Scale" type="string"><![CDATA[2.0]]></param>
 <param name="Texture/Pattern/Pattern" type="string"><![CDATA[AAAA GAA BAA CAAE dGVz dABk ZGQ=]]></param>
</Preset>)XML";
    // base64 of pat_buf (28 bytes):
    // Construct base64 directly:
    QByteArray raw_pat(reinterpret_cast<const char *>(pat_buf.data()), static_cast<qsizetype>(pat_buf.size()));
    std::string b64_str = raw_pat.toBase64().toStdString();
    std::string xml_with_b64 = std::string(R"XML(<Preset name="Pattern_Test" paintopid="paintbrush">
 <param name="Texture/Enabled" type="string"><![CDATA[true]]></param>
 <param name="Texture/Pattern/Name" type="string"><![CDATA[InlinePat]]></param>
 <param name="Texture/Pattern/Scale" type="string"><![CDATA[2.5]]></param>
 <param name="Texture/Pattern/Pattern" type="string"><![CDATA[)XML") + b64_str + R"XML(]]></param>
</Preset>)XML";

    Preset pat_preset;
    std::string perr;
    check(kreart::resources::parse_preset_xml(xml_with_b64, pat_preset, perr), "pattern preset parses");
    auto p_settings = pat_preset.brush_settings(base, &database);
    check(p_settings.texture_enabled, "preset texture is enabled");
    check(close_to(p_settings.texture_scale, 2.5), "preset texture scale is 2.5");
    check(p_settings.texture != nullptr, "preset has texture mask");
    check(p_settings.texture && p_settings.texture->width() == 2 && p_settings.texture->height() == 2,
          "preset texture mask has parsed width and height 2x2");
  }
  // Test Generalized Dynamics routing from Preset XML
  {
    const char *kDynamicsXml = R"XML(<Preset name="Dynamics_Test" paintopid="paintbrush">
 <param name="SizeSensor" type="string"><![CDATA[<sensor id="speed"><curve>0.0,0.2;1.0,0.8</curve></sensor>]]></param>
 <param name="FlowSensor" type="string"><![CDATA[<sensor id="pressure"><curve>0.0,0.1;1.0,0.9</curve></sensor>]]></param>
 <param name="RotationSensor" type="string"><![CDATA[<sensor id="fuzzy"><curve>0.0,0.0;1.0,1.0</curve></sensor>]]></param>
 <param name="RateSensor" type="string"><![CDATA[<sensor id="distance"><curve>0.0,1.0;1.0,0.0</curve></sensor>]]></param>
</Preset>)XML";
    Preset dyn_preset;
    std::string derr;
    check(kreart::resources::parse_preset_xml(kDynamicsXml, dyn_preset, derr), "dynamics preset parses");
    auto ds = dyn_preset.brush_settings(base, &database);

    check(ds.size_dynamics.active(), "size dynamics curve is active");
    check(ds.size_dynamics.sensor_type() == kreart::brush::SensorType::SPEED, "size dynamics mapped to SPEED");
    check(close_to(ds.size_dynamics.evaluate(0.5), 0.5), "size dynamics curve evaluates correctly at 0.5");

    check(ds.flow_dynamics.active(), "flow dynamics curve is active");
    check(ds.flow_dynamics.sensor_type() == kreart::brush::SensorType::PRESSURE, "flow dynamics mapped to PRESSURE");

    check(ds.angle_dynamics.active(), "angle dynamics curve is active");
    check(ds.angle_dynamics.sensor_type() == kreart::brush::SensorType::FUZZY, "rotation dynamics mapped to FUZZY");

    check(ds.smudge_dynamics.active(), "smudge rate dynamics curve is active");
    check(ds.smudge_dynamics.sensor_type() == kreart::brush::SensorType::DISTANCE, "smudge rate mapped to DISTANCE");
  }


  if (failures == 0) {
    std::puts("kreart resource tests: all checks passed");
    return 0;
  }
  std::fprintf(stderr, "kreart resource tests: %d failure(s)\n", failures);
  return 1;
}
