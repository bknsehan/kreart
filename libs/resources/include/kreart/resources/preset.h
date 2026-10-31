#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "kreart/brush/round_brush.h"

namespace kreart::resources {

class ResourceDatabase;

struct PresetParameter {
  std::string type;
  std::string value;
};

struct BrushMask {
  std::string type;
  std::string id;
  double diameter = 0.0;
  double ratio = 1.0;
  double horizontal_fade = 1.0;
  double vertical_fade = 1.0;
  int spikes = 2;
  bool antialias_edges = true;
  std::map<std::string, std::string, std::less<>> attributes;
};

struct BrushDefinition {
  std::string type;
  std::string raw_xml;
  std::map<std::string, std::string, std::less<>> attributes;
  std::optional<BrushMask> mask;

  std::string attribute(std::string_view name, std::string fallback = {}) const;
  double number_attribute(std::string_view name, double fallback = 0.0) const;
  bool bool_attribute(std::string_view name, bool fallback = false) const;
};

struct Preset {
  std::filesystem::path path;
  // Krita's internal preset name. It often starts with a sort prefix such as
  // "b)_". The display name normalizes that form for the resource browser.
  std::string name;
  std::string display_name;
  std::string paintop_id;
  // The PNG "version" text chunk, for example "2.2". Empty for plain/gzip XML.
  std::string format_version;

  // Every root attribute and <param> is kept, including fields Kreart does not
  // understand yet. This is the preservation contract for future preset writing
  // and compatibility.
  std::map<std::string, std::string, std::less<>> attributes;
  std::map<std::string, PresetParameter, std::less<>> parameters;
  std::optional<BrushDefinition> brush;
  std::optional<brush::Settings> direct_settings;
  std::string category = "General";
  const PresetParameter *find_parameter(std::string_view name) const;
  std::string value(std::string_view name, std::string fallback = {}) const;
  double number(std::string_view name, double fallback = 0.0) const;
  bool boolean(std::string_view name, bool fallback = false) const;

  bool is_eraser() const;

  // Applies the subset of Krita settings that the current round brush can
  // represent. Unsupported sensor curves and brush tips remain preserved in the
  // parameter map for later engines.
  brush::Settings brush_settings(const brush::Settings &base,
                                 const ResourceDatabase *database = nullptr) const;
};

struct PresetFileContents {
  std::string xml;
  std::string format_version;
};

std::string preset_display_name(std::string_view name);

// Reads a .kpp file in any of the containers Krita has used: a PNG with a
// "preset" zTXt/tEXt/iTXt chunk, a gzip-compressed XML document, or plain XML.
bool read_preset_file(const std::filesystem::path &path, PresetFileContents &contents,
                      std::string &error);

bool parse_preset_xml(std::string_view xml, Preset &preset, std::string &error);

bool load_preset(const std::filesystem::path &path, Preset &preset, std::string &error);

} // namespace kreart::resources
