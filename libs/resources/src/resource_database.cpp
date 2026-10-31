#include "kreart/resources/resource_database.h"
#include <fstream>
#include <zip.h>
#include "kreart/brush/brush_tip.h"
#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>

namespace kreart::resources {
namespace {

std::string lower_ascii(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

void append_unique(std::vector<std::filesystem::path> &paths, const std::filesystem::path &path) {
  if (path.empty()) {
    return;
  }
  if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
    paths.push_back(path);
  }
}

std::vector<std::string> split_paths(std::string_view value) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find(':', start);
    const std::string_view part =
        end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start);
    if (!part.empty()) {
      result.emplace_back(part);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return result;
}

} // namespace
std::string categorize_preset(const Preset &preset) {
  if (!preset.category.empty() && preset.category != "General") {
    return preset.category;
  }
  if (preset.is_eraser()) {
    return "Erasers";
  }
  const std::string lower = lower_ascii(preset.path.filename().string() + " " + preset.name);
  if (lower.rfind("a)_", 0) == 0 || lower.find("eraser") != std::string::npos) {
    return "Erasers";
  }
  if (lower.rfind("c)_", 0) == 0 || lower.find("sketch") != std::string::npos ||
      lower.find("pencil") != std::string::npos) {
    return "Sketch";
  }
  if (lower.rfind("d)_", 0) == 0 || lower.find("ink") != std::string::npos) {
    return "Ink";
  }
  if (lower.rfind("b)_", 0) == 0 || lower.find("digital") != std::string::npos ||
      lower.find("basic") != std::string::npos) {
    return "Digital";
  }
  if (lower.rfind("j)_", 0) == 0 || lower.find("water") != std::string::npos ||
      lower.find("paint") != std::string::npos) {
    return "Paint";
  }
  if (lower.rfind("l)_", 0) == 0 || lower.find("texture") != std::string::npos ||
      lower.find("pattern") != std::string::npos) {
    return "Textures";
  }
  if (lower.rfind("k)_", 0) == 0 || lower.find("splat") != std::string::npos ||
      lower.find("fx") != std::string::npos) {
    return "FX";
  }
  return "General";
}


void ResourceDatabase::add_search_path(std::filesystem::path path) {
  append_unique(search_paths_, path);
}

void ResourceDatabase::clear_search_paths() {
  search_paths_.clear();
}

void ResourceDatabase::clear() {
  presets_.clear();
  categories_.clear();
  category_presets_.clear();
  tip_cache_.clear();
  std::lock_guard<std::mutex> lock(pattern_mutex_);
  pattern_cache_.clear();
}
ScanResult ResourceDatabase::scan(bool scan_bundles, bool include_krita) {
  ScanResult result;
  clear();

  if (scan_bundles) {
    const std::filesystem::path cache_root = bundle_cache_directory();
    for (const auto &bundle : standard_bundle_paths(include_krita)) {
    const std::filesystem::path bundle_dest = cache_root / bundle.stem();
    const std::filesystem::path cache_presets = bundle_dest / "presets";
    std::error_code ec;
    bool need_unpack = !std::filesystem::exists(cache_presets, ec);
    if (!need_unpack) {
      auto bundle_time = std::filesystem::last_write_time(bundle, ec);
      auto cache_time = std::filesystem::last_write_time(cache_presets, ec);
      if (bundle_time > cache_time) {
        need_unpack = true;
      }
    }
    if (need_unpack) {
      unpack_bundle(bundle, bundle_dest);
    }
    if (std::filesystem::exists(cache_presets, ec)) {
      append_unique(search_paths_, cache_presets);
    }
  }
  }

  std::map<std::string, Preset> unique_presets;
  for (const std::filesystem::path &root : search_paths_) {
    std::error_code directory_error;
    if (!std::filesystem::is_directory(root, directory_error)) {
      continue;
    }

    std::vector<std::filesystem::path> files;
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator iterator(root, iteration_error);
         !iteration_error && iterator != std::filesystem::directory_iterator();
         iterator.increment(iteration_error)) {
      const std::filesystem::path &path = iterator->path();
      if (lower_ascii(path.extension().string()) != ".kpp") {
        continue;
      }
      std::error_code type_error;
      if (iterator->is_regular_file(type_error)) {
        files.push_back(path);
      }
    }

    if (iteration_error) {
      result.diagnostics.push_back(
          {root, "Could not read preset directory: " + iteration_error.message()});
      continue;
    }

    std::sort(files.begin(), files.end(), [](const auto &left, const auto &right) {
      return left.filename().string() < right.filename().string();
    });

    for (const std::filesystem::path &path : files) {
      ++result.discovered;
      Preset preset;
      std::string error;
      if (!load_preset(path, preset, error)) {
        ++result.failed;
        result.diagnostics.push_back({path, std::move(error)});
        continue;
      }

      // Later search paths override earlier ones with the same filename. This
      // matches Krita's user-resource-over-system-resource behavior.
      unique_presets[path.filename().string()] = std::move(preset);
    }
  }
  if (unique_presets.empty()) {
    auto add_p = [&](std::string name, std::string category, brush::Settings s) {
      Preset p;
      p.name = name;
      p.display_name = name;
      p.category = category;
      p.direct_settings = s;
      unique_presets[name] = std::move(p);
    };

    // 1. Pencil (HB Sketch)
    {
      brush::Settings s;
      s.size = 12.0;
      s.opacity = 0.85;
      s.hardness = 0.75;
      s.spacing = 0.08;
      s.pressure_curve = 1.1;
      s.texture_enabled = true;
      s.texture = brush::TextureMask::create_rough_paper(128);
      s.texture_scale = 1.0;
      s.texture_strength = 0.65;
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.25;
      add_p("Pencil (HB Sketch)", "Sketch", s);
    }

    // 2. G-Pen (Inking)
    {
      brush::Settings s;
      s.size = 8.0;
      s.opacity = 1.0;
      s.hardness = 0.95;
      s.spacing = 0.05;
      s.pressure_curve = 0.85;
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.45;
      add_p("G-Pen (Inking)", "Ink", s);
    }

    // 3. Studio Hard Round
    {
      brush::Settings s;
      s.size = 24.0;
      s.opacity = 1.0;
      s.hardness = 0.95;
      s.spacing = 0.10;
      s.smoothing_mode = brush::SmoothingMode::BASIC;
      s.smoothing = 0.30;
      add_p("Studio Hard Round", "Digital", s);
    }

    // 4. Soft Airbrush
    {
      brush::Settings s;
      s.size = 64.0;
      s.opacity = 0.35;
      s.hardness = 0.0;
      s.spacing = 0.12;
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.40;
      add_p("Soft Airbrush", "Paint", s);
    }

    // 5. Oil Paint (Wet Blend)
    {
      brush::Settings s;
      s.size = 36.0;
      s.opacity = 0.90;
      s.hardness = 0.85;
      s.spacing = 0.08;
      s.color_rate = 0.65;
      s.smudge_rate = 0.55;
      s.texture_enabled = true;
      s.texture = brush::TextureMask::create_canvas_grain(128);
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.40;
      add_p("Oil Paint (Wet Blend)", "Paint", s);
    }

    // 6. Watercolor Wash
    {
      brush::Settings s;
      s.size = 48.0;
      s.opacity = 0.45;
      s.hardness = 0.30;
      s.spacing = 0.09;
      s.texture_enabled = true;
      s.texture = brush::TextureMask::create_rough_paper(128);
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.45;
      add_p("Watercolor Wash", "Paint", s);
    }

    // 7. Flat Marker
    {
      brush::Settings s;
      s.size = 32.0;
      s.opacity = 0.70;
      s.hardness = 0.90;
      s.spacing = 0.06;
      s.tilt_enabled = true;
      s.tilt_strength = 0.70;
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.30;
      add_p("Flat Marker", "Digital", s);
    }

    // 8. Charcoal Tooth
    {
      brush::Settings s;
      s.size = 28.0;
      s.opacity = 0.85;
      s.hardness = 0.80;
      s.spacing = 0.08;
      s.texture_enabled = true;
      s.texture = brush::TextureMask::create_charcoal_tooth(128);
      s.texture_strength = 0.80;
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.35;
      add_p("Charcoal (Tooth)", "Sketch", s);
    }

    // 9. Precision Eraser
    {
      brush::Settings s;
      s.mode = brush::Mode::ERASE;
      s.size = 18.0;
      s.opacity = 1.0;
      s.hardness = 0.95;
      s.spacing = 0.08;
      s.smoothing_mode = brush::SmoothingMode::BASIC;
      s.smoothing = 0.20;
      add_p("Precision Eraser", "Erasers", s);
    }

    // 10. Soft Kneaded Eraser
    {
      brush::Settings s;
      s.mode = brush::Mode::ERASE;
      s.size = 42.0;
      s.opacity = 0.65;
      s.hardness = 0.20;
      s.spacing = 0.12;
      s.smoothing_mode = brush::SmoothingMode::WEIGHTED;
      s.smoothing = 0.35;
      add_p("Soft Kneaded Eraser", "Erasers", s);
    }
  }

  presets_.reserve(unique_presets.size());
  for (auto &[filename, preset] : unique_presets) {
    (void)filename;
    presets_.push_back(std::move(preset));
  }

  std::sort(presets_.begin(), presets_.end(), [](const Preset &left, const Preset &right) {
    const std::string left_name = lower_ascii(left.display_name);
    const std::string right_name = lower_ascii(right.display_name);
    if (left_name != right_name) {
      return left_name < right_name;
    }
    return left.path.filename().string() < right.path.filename().string();
  });
  // Populate categories
  category_presets_.clear();
  categories_.clear();
  categories_.push_back("All");

  for (std::size_t i = 0; i < presets_.size(); ++i) {
    category_presets_["All"].push_back(i);
    const std::string cat = categorize_preset(presets_[i]);
    category_presets_[cat].push_back(i);
  }

  for (const auto &[cat_name, _] : category_presets_) {
    if (cat_name != "All") {
      categories_.push_back(cat_name);
    }
  }


  result.loaded = presets_.size();
  return result;
}

std::vector<std::filesystem::path> ResourceDatabase::standard_preset_paths(bool include_krita) {
  std::vector<std::filesystem::path> paths;

  std::string data_dirs = "/usr/local/share:/usr/share";
  if (const char *environment = std::getenv("XDG_DATA_DIRS");
      environment != nullptr && environment[0] != '\0') {
    data_dirs = environment;
  }

  std::vector<std::string> system_data_dirs = split_paths(data_dirs);
  // XDG_DATA_DIRS lists higher-priority directories first. Preset scanning lets
  // later roots override earlier ones, so walk this list backwards.
  std::reverse(system_data_dirs.begin(), system_data_dirs.end());
  for (const std::string &directory : system_data_dirs) {
    const std::filesystem::path base(directory);
    if (include_krita) {
      append_unique(paths, base / "krita" / "paintoppresets");
    }
    append_unique(paths, base / "kreart" / "presets");
  }

  std::filesystem::path user_data;
  if (const char *environment = std::getenv("XDG_DATA_HOME");
      environment != nullptr && environment[0] != '\0') {
    const std::filesystem::path candidate(environment);
    if (candidate.is_absolute()) {
      user_data = candidate;
    }
  }
  if (user_data.empty()) {
    if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      user_data = std::filesystem::path(home) / ".local" / "share";
    }
  }
  if (!user_data.empty()) {
    if (include_krita) {
      append_unique(paths, user_data / "krita" / "paintoppresets");
    }
    append_unique(paths, user_data / "kreart" / "presets");
  }
  // Windows platform paths: %APPDATA%/kreart and %LOCALAPPDATA%/krita
  if (const char *appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
    append_unique(paths, std::filesystem::path(appdata) / "kreart" / "presets");
  }
  if (include_krita) {
    if (const char *localappdata = std::getenv("LOCALAPPDATA"); localappdata != nullptr && localappdata[0] != '\0') {
      append_unique(paths, std::filesystem::path(localappdata) / "krita" / "paintoppresets");
    }
  }

  // Android and cross-platform Qt standard paths
  QString appDataLoc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!appDataLoc.isEmpty()) {
    std::filesystem::path appDataPath(appDataLoc.toStdString());
    append_unique(paths, appDataPath / "presets");
    if (include_krita) {
      append_unique(paths, appDataPath / "krita" / "paintoppresets");
    }
  }

  std::error_code ec;
  const auto cwd = std::filesystem::current_path(ec);
  if (!ec && !cwd.empty()) {
    const std::filesystem::path local_presets = cwd / "data" / "presets";
    if (std::filesystem::is_directory(local_presets, ec)) {
      append_unique(paths, local_presets);
    }
    const std::filesystem::path parent_presets = cwd.parent_path() / "data" / "presets";
    if (std::filesystem::is_directory(parent_presets, ec)) {
      append_unique(paths, parent_presets);
    }
  }

  // Optional development and power-user override. Multiple directories use the
  // same ':' separator as XDG_DATA_DIRS, with later directories taking priority.
  if (const char *environment = std::getenv("KREART_PRESET_PATH"); environment != nullptr) {
    for (const std::string &directory : split_paths(environment)) {
      append_unique(paths, std::filesystem::path(directory));
    }
  }

  return paths;
}
std::vector<const Preset *>
ResourceDatabase::presets_for_category(std::string_view category) const {
  std::vector<const Preset *> result;
  auto it = category_presets_.find(category);
  if (it == category_presets_.end()) {
    it = category_presets_.find("All");
  }
  if (it != category_presets_.end()) {
    result.reserve(it->second.size());
    for (std::size_t idx : it->second) {
      if (idx < presets_.size()) {
        result.push_back(&presets_[idx]);
      }
    }
  }
  return result;
}
std::filesystem::path ResourceDatabase::user_resource_directory() {
  std::filesystem::path user_data;
  if (const char *env = std::getenv("XDG_DATA_HOME"); env != nullptr && env[0] != '\0') {
    const std::filesystem::path candidate(env);
    if (candidate.is_absolute()) {
      user_data = candidate;
    }
  }
  if (user_data.empty()) {
    if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      user_data = std::filesystem::path(home) / ".local" / "share";
    }
  }
  if (user_data.empty()) {
    if (const char *appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
      return std::filesystem::path(appdata) / "kreart";
    }
    QString qdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!qdata.isEmpty()) {
      return std::filesystem::path(qdata.toStdString());
    }
  }
  return user_data / "kreart";
}

std::filesystem::path ResourceDatabase::bundle_cache_directory() {
  std::filesystem::path user_cache;
  if (const char *env = std::getenv("XDG_CACHE_HOME"); env != nullptr && env[0] != '\0') {
    const std::filesystem::path candidate(env);
    if (candidate.is_absolute()) {
      user_cache = candidate;
    }
  }
  if (user_cache.empty()) {
    if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      user_cache = std::filesystem::path(home) / ".cache";
    }
  }
  if (user_cache.empty()) {
    QString qcache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!qcache.isEmpty()) {
      return std::filesystem::path(qcache.toStdString()) / "bundles";
    }
  }
  return user_cache / "kreart" / "bundles";
}

BundleImportResult ResourceDatabase::unpack_bundle(const std::filesystem::path &bundle_path,
                                                  const std::filesystem::path &destination_dir) {
  BundleImportResult result;
  result.bundle_name = bundle_path.stem().string();

  int err = 0;
  zip_t *za = zip_open(bundle_path.string().c_str(), ZIP_RDONLY, &err);
  if (!za) {
    result.error = "Failed to open bundle archive: " + std::to_string(err);
    result.success = false;
    return result;
  }

  const std::filesystem::path presets_dir = destination_dir / "presets";
  const std::filesystem::path brushes_dir = destination_dir / "brushes";
  const std::filesystem::path patterns_dir = destination_dir / "patterns";

  std::error_code ec;
  std::filesystem::create_directories(presets_dir, ec);
  std::filesystem::create_directories(brushes_dir, ec);
  std::filesystem::create_directories(patterns_dir, ec);

  const zip_int64_t num_entries = zip_get_num_entries(za, 0);
  for (zip_int64_t i = 0; i < num_entries; ++i) {
    const char *name = zip_get_name(za, i, 0);
    if (!name) continue;

    std::string entry_name(name);
    std::filesystem::path target_dir;
    bool is_preset = false, is_tip = false, is_pattern = false;

    if (entry_name.rfind("paintoppresets/", 0) == 0 && entry_name.ends_with(".kpp")) {
      target_dir = presets_dir;
      is_preset = true;
    } else if (entry_name.rfind("brushes/", 0) == 0) {
      target_dir = brushes_dir;
      is_tip = true;
    } else if (entry_name.rfind("patterns/", 0) == 0) {
      target_dir = patterns_dir;
      is_pattern = true;
    } else {
      continue;
    }

    std::string filename = std::filesystem::path(entry_name).filename().string();
    if (filename.empty()) continue;

    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(za, i, 0, &st) != 0 || st.size <= 0) {
      continue;
    }

    zip_file_t *zf = zip_fopen_index(za, i, 0);
    if (!zf) continue;

    std::vector<char> buffer(st.size);
    zip_int64_t bytes_read = zip_fread(zf, buffer.data(), st.size);
    zip_fclose(zf);

    if (bytes_read > 0) {
      const std::filesystem::path out_file = target_dir / filename;
      std::ofstream out(out_file, std::ios::binary);
      if (out) {
        out.write(buffer.data(), bytes_read);
        if (is_preset) ++result.presets_imported;
        else if (is_tip) ++result.tips_imported;
        else if (is_pattern) ++result.patterns_imported;
      }
    }
  }

  zip_close(za);
  result.success = (result.presets_imported > 0 || result.tips_imported > 0);
  return result;
}


std::vector<std::filesystem::path> ResourceDatabase::standard_brush_paths(bool include_krita) {
  std::vector<std::filesystem::path> paths;
  for (const auto &preset_dir : standard_preset_paths(include_krita)) {
    append_unique(paths, preset_dir.parent_path() / "brushes");
  }
  const std::filesystem::path cache_root = bundle_cache_directory();
  for (const auto &bundle : standard_bundle_paths(include_krita)) {
    append_unique(paths, cache_root / bundle.stem() / "brushes");
  }
  return paths;
}
std::vector<std::filesystem::path> ResourceDatabase::standard_pattern_paths(bool include_krita) {
  std::vector<std::filesystem::path> paths;
  append_unique(paths, user_resource_directory() / "patterns");
  for (const auto &preset_dir : standard_preset_paths(include_krita)) {
    append_unique(paths, preset_dir.parent_path() / "patterns");
  }
  append_unique(paths, std::filesystem::path("/usr/share/krita/patterns"));
  append_unique(paths, std::filesystem::path("/usr/local/share/krita/patterns"));
  const std::filesystem::path cache_root = bundle_cache_directory();
  for (const auto &bundle : standard_bundle_paths(include_krita)) {
    append_unique(paths, cache_root / bundle.stem() / "patterns");
  }
  return paths;
}

std::vector<std::filesystem::path> ResourceDatabase::standard_bundle_paths(bool include_krita) {
  std::vector<std::filesystem::path> paths;
  for (const auto &preset_dir : standard_preset_paths(include_krita)) {
    const std::filesystem::path bundle_dir = preset_dir.parent_path() / "bundles";
    std::error_code ec;
    if (std::filesystem::is_directory(bundle_dir, ec)) {
      for (const auto &entry : std::filesystem::directory_iterator(bundle_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bundle") {
          append_unique(paths, entry.path());
        }
      }
    }
    const std::filesystem::path parent_dir = preset_dir.parent_path();
    if (std::filesystem::is_directory(parent_dir, ec)) {
      for (const auto &entry : std::filesystem::directory_iterator(parent_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bundle") {
          append_unique(paths, entry.path());
        }
      }
    }
    const std::filesystem::path user_bundles = user_resource_directory() / "bundles";
    if (std::filesystem::is_directory(user_bundles, ec)) {
      for (const auto &entry : std::filesystem::directory_iterator(user_bundles, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bundle") {
          append_unique(paths, entry.path());
        }
      }
    }
  }
  return paths;
}

std::shared_ptr<brush::BrushTip>
ResourceDatabase::find_brush_tip(std::string_view filename) const {
  if (filename.empty()) {
    return nullptr;
  }

  auto cache_it = tip_cache_.find(filename);
  if (cache_it != tip_cache_.end()) {
    return cache_it->second;
  }

  // 1. Loose files
  for (const auto &root : standard_brush_paths()) {
    const std::filesystem::path candidate = root / filename;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      std::ifstream f(candidate, std::ios::binary);
      if (f) {
        std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(f), {});
        if (auto tip = brush::BrushTip::load_from_memory(bytes.data(), bytes.size(), filename)) {
          tip_cache_[std::string(filename)] = tip;
          return tip;
        }
      }
    }
  }

  // 2. Bundles
  for (const auto &bundle : standard_bundle_paths()) {
    int err = 0;
    zip_t *za = zip_open(bundle.string().c_str(), ZIP_RDONLY, &err);
    if (!za) continue;

    const std::string entry_name = "brushes/" + std::string(filename);
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(za, entry_name.c_str(), 0, &st) == 0 && st.size > 0) {
      zip_file_t *zf = zip_fopen(za, entry_name.c_str(), 0);
      if (zf) {
        std::vector<std::uint8_t> bytes(st.size);
        zip_int64_t rd = zip_fread(zf, bytes.data(), st.size);
        zip_fclose(zf);
        if (rd > 0) {
          bytes.resize(static_cast<std::size_t>(rd));
          if (auto tip = brush::BrushTip::load_from_memory(bytes.data(), bytes.size(), filename)) {
            zip_close(za);
            tip_cache_[std::string(filename)] = tip;
            return tip;
          }
        }
       }
    }
    zip_close(za);
  }

  return nullptr;
}
std::shared_ptr<brush::TextureMask>
ResourceDatabase::find_pattern(std::string_view name) const {
  if (name.empty()) {
    return nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(pattern_mutex_);
    auto cache_it = pattern_cache_.find(name);
    if (cache_it != pattern_cache_.end()) {
      return cache_it->second;
    }
  }

  std::vector<std::string> candidates;
  candidates.push_back(std::string(name));
  if (!name.ends_with(".pat") && !name.ends_with(".png") && !name.ends_with(".jpg")) {
    candidates.push_back(std::string(name) + ".pat");
    candidates.push_back(std::string(name) + ".png");
  }

  // 1. Loose files
  for (const auto &root : standard_pattern_paths(true)) {
    for (const auto &cand : candidates) {
      const std::filesystem::path candidate = root / cand;
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec)) {
        std::ifstream f(candidate, std::ios::binary);
        if (f) {
          std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(f), {});
          if (auto mask = brush::TextureMask::load_from_memory(bytes.data(), bytes.size(), std::string(name))) {
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            pattern_cache_[std::string(name)] = mask;
            return mask;
          }
        }
      }
    }
  }

  // 2. Bundles
  for (const auto &bundle : standard_bundle_paths(true)) {
    int err = 0;
    zip_t *za = zip_open(bundle.string().c_str(), ZIP_RDONLY, &err);
    if (!za) continue;

    for (const auto &cand : candidates) {
      const std::string entry_name = "patterns/" + cand;
      zip_stat_t st;
      zip_stat_init(&st);
      if (zip_stat(za, entry_name.c_str(), 0, &st) == 0 && st.size > 0) {
        zip_file_t *zf = zip_fopen(za, entry_name.c_str(), 0);
        if (zf) {
          std::vector<std::uint8_t> bytes(st.size);
          zip_int64_t rd = zip_fread(zf, bytes.data(), st.size);
          zip_fclose(zf);
          if (rd > 0) {
            bytes.resize(static_cast<std::size_t>(rd));
            if (auto mask = brush::TextureMask::load_from_memory(bytes.data(), bytes.size(), std::string(name))) {
              zip_close(za);
              std::lock_guard<std::mutex> lock(pattern_mutex_);
              pattern_cache_[std::string(name)] = mask;
              return mask;
            }
          }
        }
      }
    }
    zip_close(za);
  }

  return nullptr;
}

} // namespace kreart::resources
