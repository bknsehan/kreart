#include "kreart/io/autosave.h"

#include <cstdlib>
#include <system_error>
#include <QStandardPaths>
#include <QString>
#include "kreart/io/kra.h"

namespace kreart::io {
namespace {

std::filesystem::path get_cache_autosave_dir() {
  const char *xdg_cache = std::getenv("XDG_CACHE_HOME");
  std::filesystem::path base;
  if (xdg_cache && *xdg_cache) {
    base = xdg_cache;
  } else {
    QString qcache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (!qcache.isEmpty()) {
      base = qcache.toStdString();
    } else {
      const char *home = std::getenv("HOME");
      base = home ? std::filesystem::path(home) / ".cache" : std::filesystem::temp_directory_path();
    }
  }
  std::filesystem::path autosave_dir = base / "kreart" / "autosave";
  std::error_code ec;
  std::filesystem::create_directories(autosave_dir, ec);
  return autosave_dir;
}

} // namespace

std::filesystem::path autosave_path_for(const std::filesystem::path &original_path) {
  if (original_path.empty()) {
    return untitled_autosave_path();
  }
  return original_path.parent_path() / ("~" + original_path.filename().string());
}

std::filesystem::path untitled_autosave_path() {
  return get_cache_autosave_dir() / "~untitled.kra";
}

bool has_autosave(const std::filesystem::path &original_path,
                  std::filesystem::path &autosave_path) {
  const std::filesystem::path candidate = autosave_path_for(original_path);
  std::error_code ec;
  if (!std::filesystem::exists(candidate, ec)) {
    return false;
  }

  if (original_path.empty() || !std::filesystem::exists(original_path, ec)) {
    autosave_path = candidate;
    return true;
  }

  const auto original_time = std::filesystem::last_write_time(original_path, ec);
  const auto auto_time = std::filesystem::last_write_time(candidate, ec);
  if (!ec && auto_time > original_time) {
    autosave_path = candidate;
    return true;
  }

  return false;
}

bool perform_autosave(const core::Document &document, const std::filesystem::path &original_path,
                      std::filesystem::path &saved_path, std::string &error) {
  saved_path = autosave_path_for(original_path);
  return write_kra(saved_path, document, error);
}

void remove_autosave(const std::filesystem::path &original_path) {
  const std::filesystem::path candidate = autosave_path_for(original_path);
  std::error_code ec;
  std::filesystem::remove(candidate, ec);
}

std::vector<std::filesystem::path> find_orphaned_autosaves() {
  std::vector<std::filesystem::path> result;
  const std::filesystem::path dir = get_cache_autosave_dir();
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec)) {
    return result;
  }

  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file()) {
      const std::string name = entry.path().filename().string();
      if ((name.rfind("~", 0) == 0 || name.rfind(".", 0) == 0) &&
          entry.path().extension() == ".kra") {
        result.push_back(entry.path());
      }
    }
  }
  return result;
}

} // namespace kreart::io
