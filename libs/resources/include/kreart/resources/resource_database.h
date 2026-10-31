#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <vector>

#include "kreart/resources/preset.h"

namespace kreart::resources {

struct ResourceDiagnostic {
  std::filesystem::path path;
  std::string message;
};
struct BundleImportResult {
  std::size_t presets_imported = 0;
  std::size_t tips_imported = 0;
  std::size_t patterns_imported = 0;
  std::string bundle_name;
  std::string error;
  bool success = false;
};


struct ScanResult {
  // Number of .kpp files encountered, including files that were later replaced
  // by a higher-priority root and files that failed to parse.
  std::size_t discovered = 0;
  // Number of unique presets available after the scan.
  std::size_t loaded = 0;
  std::size_t failed = 0;
  std::vector<ResourceDiagnostic> diagnostics;
};

class ResourceDatabase {
public:
  // Search paths are scanned in order. A later path overrides an earlier file
  // with the same filename, which lets the user's Krita presets replace system
  // presets. scan() rebuilds the database from the current search paths.
  void add_search_path(std::filesystem::path path);
  void clear_search_paths();
  const std::vector<std::filesystem::path> &search_paths() const {
    return search_paths_;
  }

  ScanResult scan(bool scan_bundles = true, bool include_krita = false);
  void clear();

  const std::vector<Preset> &presets() const {
    return presets_;
  }
  std::size_t size() const {
    return presets_.size();
  }
  bool empty() const {
    return presets_.empty();
  }
  const std::vector<std::string> &categories() const {
    return categories_;
  }
  std::vector<const Preset *> presets_for_category(std::string_view category) const;

  std::shared_ptr<brush::BrushTip> find_brush_tip(std::string_view filename) const;
  std::shared_ptr<brush::TextureMask> find_pattern(std::string_view name) const;


  static std::vector<std::filesystem::path> standard_brush_paths(bool include_krita = false);
  static std::vector<std::filesystem::path> standard_pattern_paths(bool include_krita = false);
  static std::vector<std::filesystem::path> standard_bundle_paths(bool include_krita = false);
  static BundleImportResult unpack_bundle(const std::filesystem::path &bundle_path,
                                         const std::filesystem::path &destination_dir);
  static std::filesystem::path user_resource_directory();
  static std::filesystem::path bundle_cache_directory();


  // Krita's standard preset locations, ordered from lowest to highest priority.
  // Missing directories are ignored by scan().
  static std::vector<std::filesystem::path> standard_preset_paths(bool include_krita = false);

private:
  std::vector<std::filesystem::path> search_paths_;
  std::vector<Preset> presets_;
  std::vector<std::string> categories_;
  std::map<std::string, std::vector<std::size_t>, std::less<>> category_presets_;
  mutable std::map<std::string, std::shared_ptr<brush::BrushTip>, std::less<>> tip_cache_;
  mutable std::mutex pattern_mutex_;
  mutable std::map<std::string, std::shared_ptr<brush::TextureMask>, std::less<>> pattern_cache_;
};

} // namespace kreart::resources
