#include "kreart/resources/preset.h"
#include "kreart/resources/resource_database.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zlib.h>

#include <QByteArray>
namespace kreart::resources {
namespace {

constexpr std::array<unsigned char, 8> kPngSignature{0x89, 0x50, 0x4e, 0x47,
                                                     0x0d, 0x0a, 0x1a, 0x0a};
constexpr std::size_t kMaxPresetFileSize = 64U * 1024U * 1024U;
constexpr std::size_t kMaxPresetXmlSize = 16U * 1024U * 1024U;

struct XmlDocDeleter {
  void operator()(xmlDoc *document) const {
    if (document != nullptr) {
      xmlFreeDoc(document);
    }
  }
};

using XmlDocPtr = std::unique_ptr<xmlDoc, XmlDocDeleter>;

struct InflateGuard {
  z_stream *stream = nullptr;
  ~InflateGuard() {
    if (stream != nullptr) {
      inflateEnd(stream);
    }
  }
};

std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

bool iequals(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

double parse_double(std::string_view text, double fallback) {
  text = trim(text);
  if (text.empty()) {
    return fallback;
  }
  std::string s(text);
  char *end = nullptr;
  errno = 0;
  const double result = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || errno != 0 || !std::isfinite(result)) {
    return fallback;
  }
  return result;
}

int parse_int(std::string_view text, int fallback) {
  text = trim(text);
  if (text.empty()) {
    return fallback;
  }

  int result = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return fallback;
  }
  return result;
}

bool parse_bool(std::string_view text, bool fallback) {
  text = trim(text);
  if (iequals(text, "true") || text == "1" || iequals(text, "yes") || iequals(text, "on")) {
    return true;
  }
  if (iequals(text, "false") || text == "0" || iequals(text, "no") || iequals(text, "off")) {
    return false;
  }
  return fallback;
}

std::string xml_attribute(xmlNode *node, const char *name) {
  if (node == nullptr) {
    return {};
  }

  xmlChar *value = xmlGetProp(node, reinterpret_cast<const xmlChar *>(name));
  if (value == nullptr) {
    return {};
  }
  std::string result(reinterpret_cast<const char *>(value));
  xmlFree(value);
  return result;
}

bool node_name_is(xmlNode *node, std::string_view name) {
  if (node == nullptr || node->name == nullptr) {
    return false;
  }
  return iequals(reinterpret_cast<const char *>(node->name), name);
}

XmlDocPtr read_xml_document(std::string_view xml) {
  static const bool parser_initialized = []() {
    xmlInitParser();
    return true;
  }();
  (void)parser_initialized;

  if (xml.empty()) {
    return {};
  }

  const int options = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING;
  return XmlDocPtr(
      xmlReadMemory(xml.data(), static_cast<int>(xml.size()), nullptr, nullptr, options));
}

std::string node_content(xmlNode *node) {
  xmlChar *content = xmlNodeGetContent(node);
  if (content == nullptr) {
    return {};
  }
  std::string result(reinterpret_cast<const char *>(content));
  xmlFree(content);
  return result;
}

void read_attributes(xmlNode *node, std::map<std::string, std::string, std::less<>> &attributes) {
  for (xmlAttr *attribute = node->properties; attribute != nullptr; attribute = attribute->next) {
    const std::string name(reinterpret_cast<const char *>(attribute->name));
    xmlChar *value = xmlNodeListGetString(node->doc, attribute->children, 1);
    if (value != nullptr) {
      attributes[name] = reinterpret_cast<const char *>(value);
      xmlFree(value);
    } else {
      attributes[name] = {};
    }
  }
}

bool parse_brush_definition(std::string_view xml, BrushDefinition &brush, std::string &error) {
  XmlDocPtr document = read_xml_document(xml);
  if (!document) {
    error = "Invalid brush definition XML";
    return false;
  }

  xmlNode *root = xmlDocGetRootElement(document.get());
  if (!node_name_is(root, "Brush")) {
    error = "Brush definition does not start with a Brush element";
    return false;
  }

  BrushDefinition parsed;
  parsed.raw_xml = std::string(xml);
  read_attributes(root, parsed.attributes);
  parsed.type = xml_attribute(root, "type");

  for (xmlNode *child = root->children; child != nullptr; child = child->next) {
    if (child->type != XML_ELEMENT_NODE || !node_name_is(child, "MaskGenerator")) {
      continue;
    }

    BrushMask mask;
    mask.type = xml_attribute(child, "type");
    mask.id = xml_attribute(child, "id");
    mask.diameter = parse_double(xml_attribute(child, "diameter"), 0.0);
    mask.ratio = parse_double(xml_attribute(child, "ratio"), 1.0);
    mask.horizontal_fade = parse_double(xml_attribute(child, "hfade"), 1.0);
    mask.vertical_fade = parse_double(xml_attribute(child, "vfade"), 1.0);
    mask.spikes = parse_int(xml_attribute(child, "spikes"), 2);
    mask.antialias_edges = parse_bool(xml_attribute(child, "antialiasEdges"), true);
    read_attributes(child, mask.attributes);
    parsed.mask = std::move(mask);
    break;
  }

  brush = std::move(parsed);
  error.clear();
  return true;
}

bool inflate_bytes(std::string_view compressed, int window_bits, std::size_t max_output,
                   std::string &output, std::string &error) {
  if (compressed.size() > std::numeric_limits<uInt>::max()) {
    error = "Compressed preset XML is too large";
    return false;
  }

  z_stream stream{};
  stream.next_in =
      reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data())); // zlib API is not const
  stream.avail_in = static_cast<uInt>(compressed.size());
  if (inflateInit2(&stream, window_bits) != Z_OK) {
    error = "Could not initialize zlib";
    return false;
  }
  InflateGuard guard{&stream};

  output.clear();
  std::array<unsigned char, 16384> buffer{};
  while (true) {
    stream.next_out = buffer.data();
    stream.avail_out = static_cast<uInt>(buffer.size());
    const int result = inflate(&stream, Z_NO_FLUSH);
    if (result != Z_OK && result != Z_STREAM_END) {
      error = "Could not decompress preset XML";
      return false;
    }

    const std::size_t produced = buffer.size() - stream.avail_out;
    if (output.size() + produced > max_output) {
      error = "Decompressed preset XML is too large";
      return false;
    }
    output.append(reinterpret_cast<const char *>(buffer.data()), produced);

    if (result == Z_STREAM_END) {
      break;
    }
    if (stream.avail_in == 0 && produced == 0) {
      error = "Truncated compressed preset XML";
      return false;
    }
  }

  if (output.empty()) {
    error = "Preset XML is empty";
    return false;
  }
  error.clear();
  return true;
}

std::uint32_t read_big_endian_u32(const unsigned char *data) {
  return (static_cast<std::uint32_t>(data[0]) << 24U) |
         (static_cast<std::uint32_t>(data[1]) << 16U) |
         (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
}

bool extract_png_text(std::string_view data, std::string &xml, std::string &format_version,
                      std::string &error) {
  bool found_preset = false;
  std::size_t offset = kPngSignature.size();

  while (offset + 12U <= data.size()) {
    const auto *header = reinterpret_cast<const unsigned char *>(data.data() + offset);
    const std::uint32_t length = read_big_endian_u32(header);
    const std::string_view type(data.data() + offset + 4U, 4U);
    if (length > data.size() - offset - 12U) {
      error = "PNG preset has a truncated chunk";
      return false;
    }

    const std::string_view chunk(data.data() + offset + 8U, length);

    if (type == "zTXt" || type == "tEXt") {
      const std::size_t keyword_end = chunk.find('\0');
      if (keyword_end == std::string_view::npos) {
        offset += 12U + length;
        continue;
      }
      const std::string_view keyword = chunk.substr(0, keyword_end);

      if (keyword == "version") {
        if (type == "tEXt" && keyword_end + 1U <= chunk.size()) {
          format_version = std::string(chunk.substr(keyword_end + 1U));
        }
      } else if (keyword == "preset" && !found_preset) {
        if (type == "tEXt") {
          xml = std::string(chunk.substr(keyword_end + 1U));
          found_preset = !xml.empty();
        } else {
          if (keyword_end + 2U > chunk.size()) {
            error = "PNG preset has an invalid zTXt chunk";
            return false;
          }
          const unsigned char method = static_cast<unsigned char>(chunk[keyword_end + 1U]);
          if (method != 0U) {
            error = "PNG preset uses an unsupported text compression method";
            return false;
          }
          if (!inflate_bytes(chunk.substr(keyword_end + 2U), 15, kMaxPresetXmlSize, xml, error)) {
            return false;
          }
          found_preset = !xml.empty();
        }
      }
    } else if (type == "iTXt" && !found_preset) {
      const std::size_t keyword_end = chunk.find('\0');
      if (keyword_end == std::string_view::npos || keyword_end + 3U > chunk.size()) {
        offset += 12U + length;
        continue;
      }
      const std::string_view keyword = chunk.substr(0, keyword_end);
      if (keyword != "preset") {
        offset += 12U + length;
        continue;
      }

      const unsigned char compression_flag = static_cast<unsigned char>(chunk[keyword_end + 1U]);
      const unsigned char compression_method = static_cast<unsigned char>(chunk[keyword_end + 2U]);
      std::size_t text_offset = keyword_end + 3U;

      const std::size_t language_end = chunk.find('\0', text_offset);
      if (language_end == std::string_view::npos) {
        offset += 12U + length;
        continue;
      }
      text_offset = language_end + 1U;
      const std::size_t translated_end = chunk.find('\0', text_offset);
      if (translated_end == std::string_view::npos) {
        offset += 12U + length;
        continue;
      }
      text_offset = translated_end + 1U;

      if (compression_flag == 0U) {
        xml = std::string(chunk.substr(text_offset));
        found_preset = !xml.empty();
      } else if (compression_flag == 1U && compression_method == 0U) {
        if (!inflate_bytes(chunk.substr(text_offset), 15, kMaxPresetXmlSize, xml, error)) {
          return false;
        }
        found_preset = !xml.empty();
      }
    }

    if (type == "IEND") {
      break;
    }
    offset += 12U + length;
  }

  if (!found_preset) {
    error = "PNG preset does not contain a 'preset' text chunk";
    return false;
  }
  error.clear();
  return true;
}

bool read_file(const std::filesystem::path &path, std::string &data, std::string &error) {
  std::error_code size_error;
  const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    error = "Could not read preset file size: " + size_error.message();
    return false;
  }
  if (file_size > kMaxPresetFileSize) {
    error = "Preset file is too large";
    return false;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open preset file";
    return false;
  }

  data.assign(static_cast<std::size_t>(file_size), '\0');
  if (!data.empty()) {
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input) {
      error = "Could not read preset file";
      return false;
    }
  }
  return true;
}

} // namespace

std::string BrushDefinition::attribute(std::string_view name, std::string fallback) const {
  const auto found = attributes.find(name);
  if (found == attributes.end()) {
    return fallback;
  }
  return found->second;
}

double BrushDefinition::number_attribute(std::string_view name, double fallback) const {
  const auto found = attributes.find(name);
  if (found == attributes.end()) {
    return fallback;
  }
  return parse_double(found->second, fallback);
}

bool BrushDefinition::bool_attribute(std::string_view name, bool fallback) const {
  const auto found = attributes.find(name);
  if (found == attributes.end()) {
    return fallback;
  }
  return parse_bool(found->second, fallback);
}

const PresetParameter *Preset::find_parameter(std::string_view parameter_name) const {
  const auto found = parameters.find(parameter_name);
  return found == parameters.end() ? nullptr : &found->second;
}

std::string Preset::value(std::string_view parameter_name, std::string fallback) const {
  const PresetParameter *parameter = find_parameter(parameter_name);
  return parameter == nullptr ? std::move(fallback) : parameter->value;
}

double Preset::number(std::string_view parameter_name, double fallback) const {
  const PresetParameter *parameter = find_parameter(parameter_name);
  return parameter == nullptr ? fallback : parse_double(parameter->value, fallback);
}

bool Preset::boolean(std::string_view parameter_name, bool fallback) const {
  const PresetParameter *parameter = find_parameter(parameter_name);
  return parameter == nullptr ? fallback : parse_bool(parameter->value, fallback);
}

bool Preset::is_eraser() const {
  if (direct_settings.has_value()) {
    return direct_settings->mode == brush::Mode::ERASE;
  }
  const std::string composite = value("CompositeOp");
  const std::string_view composite_name = trim(composite);
  if (iequals(composite_name, "erase") || iequals(composite_name, "eraser")) {
    return true;
  }
  return boolean("EraserMode", false) || boolean("Krita/erase", false);
}

brush::Settings Preset::brush_settings(const brush::Settings &base,
                                       const ResourceDatabase *database) const {
  if (direct_settings.has_value()) {
    brush::Settings s = *direct_settings;
    s.color = base.color;
    return s;
  }
  brush::Settings settings = base;

  if (brush) {
    if (brush->mask && brush->mask->diameter > 0.0) {
      settings.size = brush->mask->diameter;
    }

    if (const auto spacing = brush->attributes.find("spacing");
        spacing != brush->attributes.end()) {
      settings.spacing = parse_double(spacing->second, settings.spacing);
    }

    if (brush->mask) {
      settings.ratio = std::clamp(brush->mask->ratio, 0.05, 20.0);
      settings.hardness_x = std::clamp(brush->mask->horizontal_fade, 0.0, 1.0);
      settings.hardness_y = std::clamp(brush->mask->vertical_fade, 0.0, 1.0);
      settings.hardness = std::min(settings.hardness_x, settings.hardness_y);
    }
  }

  const double opacity = find_parameter("OpacityValue") != nullptr
                             ? number("OpacityValue", 1.0)
                             : number("Krita/opacity", 100.0) / 100.0;
  const double flow = number("FlowValue", 1.0);
  settings.opacity = opacity;
  settings.flow = flow;
  if (find_parameter("CompositeOp")) {
    settings.composite_op = value("CompositeOp");
  }
  // Sensor dynamics curves
  auto route_sensor = [this](std::string_view sensor_name, std::string_view common_name,
                             brush::SensorCurve &curve) {
    std::string curve_data;
    std::string sensor_meta;
    if (const auto *p = find_parameter(common_name); p && !p->value.empty()) {
      curve_data = p->value;
    }
    if (const auto *p2 = find_parameter(sensor_name); p2 && !p2->value.empty()) {
      sensor_meta = p2->value;
      if (curve_data.empty()) {
        curve_data = p2->value;
      }
    }
    if (curve_data.empty()) return;
    if (!curve.parse(curve_data)) return;

    std::string lower = sensor_meta + " " + curve_data;
    for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower.find("speed") != std::string::npos || lower.find("velocity") != std::string::npos) {
      curve.set_sensor_type(brush::SensorType::SPEED);
    } else if (lower.find("tilt") != std::string::npos || lower.find("altitude") != std::string::npos) {
      curve.set_sensor_type(brush::SensorType::TILT);
    } else if (lower.find("distance") != std::string::npos || lower.find("fade") != std::string::npos) {
      curve.set_sensor_type(brush::SensorType::DISTANCE);
    } else if (lower.find("fuzzy") != std::string::npos || lower.find("random") != std::string::npos) {
      curve.set_sensor_type(brush::SensorType::FUZZY);
    } else {
      curve.set_sensor_type(brush::SensorType::PRESSURE);
    }
  };

  route_sensor("SizeSensor", "SizecommonCurve", settings.size_dynamics);
  if (settings.size_dynamics.active()) {
    settings.size_curve = settings.size_dynamics;
  }
  route_sensor("OpacitySensor", "OpacitycommonCurve", settings.opacity_dynamics);
  if (settings.opacity_dynamics.active()) {
    settings.opacity_curve = settings.opacity_dynamics;
  }
  route_sensor("FlowSensor", "FlowcommonCurve", settings.flow_dynamics);
  route_sensor("ScatterSensor", "ScattercommonCurve", settings.scatter_dynamics);
  route_sensor("RotationSensor", "RotationcommonCurve", settings.angle_dynamics);
  route_sensor("RateSensor", "RatecommonCurve", settings.smudge_dynamics);
  // Brush tip image
  std::string tip_file = value("requiredBrushFile");
  if (tip_file.empty() && brush) {
    tip_file = brush->attribute("filename");
  }
  if (!tip_file.empty() && database) {
    settings.tip = database->find_brush_tip(tip_file);
  }
  // Texture and Pattern Mask
  if (boolean("Texture/Pattern/Enabled", false) || boolean("Texture/Enabled", false)) {
    settings.texture_enabled = true;
    settings.texture_scale = number("Texture/Pattern/Scale", 1.0);
    settings.texture_strength = number("Texture/Pattern/Strength", 0.75);
    settings.texture_contrast = number("Texture/Pattern/Contrast", 1.0);
    settings.texture_brightness = number("Texture/Pattern/Brightness", 0.0);
    settings.texture_invert = boolean("Texture/Pattern/Invert", false);
    settings.texture_offset_x = number("Texture/Pattern/OffsetX", 0.0);
    settings.texture_offset_y = number("Texture/Pattern/OffsetY", 0.0);

    std::string pattern_name = value("Texture/Pattern/PatternFileName");
    if (pattern_name.empty()) {
      pattern_name = value("Texture/Pattern/Name");
    }

    std::string pattern_b64 = value("Texture/Pattern/Pattern");
    if (!pattern_b64.empty()) {
      QByteArray decoded = QByteArray::fromBase64(
          QByteArray::fromRawData(pattern_b64.data(), static_cast<qsizetype>(pattern_b64.size())));
      if (!decoded.isEmpty()) {
        settings.texture = brush::TextureMask::load_from_memory(
            reinterpret_cast<const std::uint8_t *>(decoded.constData()),
            static_cast<std::size_t>(decoded.size()),
            pattern_name);
      }
    }

    if (!settings.texture && !pattern_name.empty() && database) {
      settings.texture = database->find_pattern(pattern_name);
    }

    if (!settings.texture) {
      settings.texture = brush::TextureMask::create_rough_paper(128);
    }
  }
  // Scatter
  if (boolean("PressureScatter", false) || find_parameter("ScatterValue") || find_parameter("Scattering/Amount")) {
    settings.scatter = std::max(0.0, number("ScatterValue", number("Scattering/Amount", 0.0)));
  }

  // Rotation / Angle
  if (find_parameter("RotationValue")) {
    const double rot_deg = number("RotationValue", 0.0);
    settings.angle = rot_deg * (M_PI / 180.0);
  }

  // Angle follow and random rotation
  if (boolean("ShapeDynamics/followDrawingAngle", false) ||
      value("RotationSensor").find("drawingangle") != std::string::npos ||
      value("RotationSensor/Type") == "drawingangle") {
    settings.angle_follow = true;
    settings.angle_follow_weight = number("ShapeDynamics/angleFollowWeight", 1.0);
  }

  if (boolean("ShapeDynamics/randomRotation", false) ||
      value("RotationSensor").find("fuzzy") != std::string::npos) {
    settings.angle_random = M_PI;
  }


  // Keep the current engine's constraints.
  settings.size = std::clamp(settings.size, 1.0, 500.0);
  settings.opacity = std::clamp(settings.opacity, 0.01, 1.0);
  settings.hardness = std::clamp(settings.hardness, 0.0, 1.0);
  settings.spacing = std::clamp(settings.spacing, 0.01, 2.0);
  return settings;
}

std::string preset_display_name(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  bool previous_space = true;
  for (const char character : trim(name)) {
    if (character == '_') {
      if (!previous_space) {
        result.push_back(' ');
        previous_space = true;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      if (!previous_space) {
        result.push_back(' ');
        previous_space = true;
      }
      continue;
    }
    result.push_back(character);
    previous_space = false;
  }
  if (!result.empty() && result.back() == ' ') {
    result.pop_back();
  }
  return result;
}

bool read_preset_file(const std::filesystem::path &path, PresetFileContents &contents,
                      std::string &error) {
  std::string data;
  if (!read_file(path, data, error)) {
    return false;
  }

  contents = {};
  if (data.size() >= kPngSignature.size() &&
      std::memcmp(data.data(), kPngSignature.data(), kPngSignature.size()) == 0) {
    return extract_png_text(data, contents.xml, contents.format_version, error);
  }

  if (data.size() >= 2U && static_cast<unsigned char>(data[0]) == 0x1fU &&
      static_cast<unsigned char>(data[1]) == 0x8bU) {
    return inflate_bytes(data, 15 + 32, kMaxPresetXmlSize, contents.xml, error);
  }

  contents.xml = std::move(data);
  if (trim(contents.xml).empty()) {
    error = "Preset file is empty";
    return false;
  }
  error.clear();
  return true;
}

bool parse_preset_xml(std::string_view xml, Preset &preset, std::string &error) {
  XmlDocPtr document = read_xml_document(xml);
  if (!document) {
    error = "Invalid preset XML";
    return false;
  }

  xmlNode *root = xmlDocGetRootElement(document.get());
  if (!node_name_is(root, "Preset")) {
    error = "Preset XML does not start with a Preset element";
    return false;
  }

  Preset parsed;
  parsed.path = preset.path;
  read_attributes(root, parsed.attributes);
  parsed.name = xml_attribute(root, "name");
  parsed.paintop_id = xml_attribute(root, "paintopid");

  for (xmlNode *child = root->children; child != nullptr; child = child->next) {
    if (child->type != XML_ELEMENT_NODE || !node_name_is(child, "param")) {
      continue;
    }

    const std::string parameter_name = xml_attribute(child, "name");
    if (parameter_name.empty()) {
      continue;
    }

    PresetParameter parameter;
    parameter.type = xml_attribute(child, "type");
    parameter.value = node_content(child);
    parsed.parameters.insert_or_assign(parameter_name, std::move(parameter));
  }

  if (parsed.paintop_id.empty()) {
    parsed.paintop_id = parsed.value("paintop");
  }
  if (parsed.name.empty()) {
    parsed.name = parsed.path.stem().string();
  }
  parsed.display_name = preset_display_name(parsed.name);

  const PresetParameter *brush_definition = parsed.find_parameter("brush_definition");
  if (brush_definition != nullptr && !trim(brush_definition->value).empty()) {
    BrushDefinition brush;
    std::string brush_error;
    if (parse_brush_definition(brush_definition->value, brush, brush_error)) {
      parsed.brush = std::move(brush);
    }
  }

  preset = std::move(parsed);
  error.clear();
  return true;
}

bool load_preset(const std::filesystem::path &path, Preset &preset, std::string &error) {
  PresetFileContents contents;
  if (!read_preset_file(path, contents, error)) {
    return false;
  }

  Preset parsed;
  parsed.path = path;
  if (!parse_preset_xml(contents.xml, parsed, error)) {
    return false;
  }
  parsed.format_version = std::move(contents.format_version);
  preset = std::move(parsed);
  return true;
}

} // namespace kreart::resources
