#include "kreart/ui/app_settings.h"
#include <QStandardPaths>
#include <QStringList>
#include <QKeySequence>
#include <QSettings>
namespace kreart::ui {

AppSettings &AppSettings::instance() {
  static AppSettings inst;
  return inst;
}

static const std::vector<AccentPalette> &accent_palette_table() {
  static const std::vector<AccentPalette> palettes = {
      {"ocean", "Ocean", "#3584e4", "#4a90e2", "rgba(53,132,228,0.14)", "#212f45", "#ffffff", "#2563eb", "#1d4ed8", "rgba(37,99,235,0.12)", "#dbeafe", "#ffffff"},
      {"violet", "Violet", "#8b5cf6", "#9d79ff", "rgba(139,92,246,0.16)", "#2c2447", "#ffffff", "#7c3aed", "#6d28d9", "rgba(124,58,237,0.12)", "#ede9fe", "#ffffff"},
      {"forest", "Forest", "#10b981", "#34d399", "rgba(16,185,129,0.15)", "#1d3b31", "#ffffff", "#059669", "#047857", "rgba(5,150,105,0.13)", "#d1fae5", "#ffffff"},
      {"ember", "Ember", "#f59e0b", "#fbbf24", "rgba(245,158,11,0.16)", "#46351c", "#1c1917", "#d97706", "#b45309", "rgba(217,119,6,0.14)", "#fef3c7", "#ffffff"},
      {"rose", "Rose", "#f43f5e", "#fb7185", "rgba(244,63,94,0.16)", "#45222b", "#ffffff", "#e11d48", "#be123c", "rgba(225,29,72,0.12)", "#ffe4e6", "#ffffff"},
  };
  return palettes;
}

const std::vector<AccentPalette> &AppSettings::accent_palettes() {
  return accent_palette_table();
}

const AccentPalette *AppSettings::accent_from_id(std::string_view id) {
  for (const auto &entry : accent_palette_table()) {
    if (id == entry.id) {
      return &entry;
    }
  }
  return nullptr;
}

AppSettings::AppSettings() {
  load();
}

QString AppSettings::theme_to_string(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::LIGHT:
      return "light";
    case ThemeMode::DARK:
      return "dark";
    case ThemeMode::SYSTEM:
    default:
      return "system";
  }
}

ThemeMode AppSettings::theme_from_string(const QString &str) {
  if (str == "light") {
    return ThemeMode::LIGHT;
  }
  if (str == "dark") {
    return ThemeMode::DARK;
  }
  return ThemeMode::SYSTEM;
}

void AppSettings::set_theme(ThemeMode mode) {
  if (theme_ != mode) {
    theme_ = mode;
    save();
    emit themeChanged(mode);
  }
}

void AppSettings::set_accent(const QString &id) {
  if (accent_ != id) {
    accent_ = id;
    save();
    emit accentChanged(id);
  }
}

void AppSettings::set_detect_krita_brushes(bool enabled) {
  if (detect_krita_brushes_ != enabled) {
    detect_krita_brushes_ = enabled;
    save();
    emit detectKritaBrushesChanged(enabled);
  }
}

void AppSettings::set_pressure_curve(const QString &curve) {
  if (pressure_curve_ != curve) {
    pressure_curve_ = curve;
    save();
    emit pressureCurveChanged(curve);
  }
}
void AppSettings::set_startup_mode(StartupMode mode) {
  if (startup_mode_ != mode) {
    startup_mode_ = mode;
    save();
    emit startupModeChanged(mode);
  }
}

QString AppSettings::projects_directory() const {
  if (projects_directory_.isEmpty()) {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Kreart Projects";
  }
  return projects_directory_;
}

void AppSettings::set_projects_directory(const QString &dir) {
  if (projects_directory_ != dir) {
    projects_directory_ = dir;
    save();
    emit projectsDirectoryChanged(dir);
  }
}

void AppSettings::set_grid_size(int size) {
  if (grid_size_ != size) {
    grid_size_ = size;
    save();
    emit gridSettingsChanged();
  }
}

void AppSettings::set_grid_color(const QColor &color) {
  if (grid_color_ != color) {
    grid_color_ = color;
    save();
    emit gridSettingsChanged();
  }
}

void AppSettings::set_show_grid(bool show) {
  if (show_grid_ != show) {
    show_grid_ = show;
    save();
    emit gridSettingsChanged();
  }
}
void AppSettings::set_transparency_follow_device(bool follow) {
  if (transparency_follow_device_ != follow) {
    transparency_follow_device_ = follow;
    save();
    emit transparencySettingsChanged();
  }
}

void AppSettings::set_transparency_mode(TransparencyMode mode) {
  if (transparency_mode_ != mode) {
    transparency_mode_ = mode;
    save();
    emit transparencySettingsChanged();
  }
}

void AppSettings::set_transparency_solid_color(const QColor &color) {
  if (transparency_solid_color_ != color) {
    transparency_solid_color_ = color;
    save();
    emit transparencySettingsChanged();
  }
}


void AppSettings::load() {
  QSettings s("io.github.kreart", "Kreart");
  theme_ = theme_from_string(s.value("appearance/theme", "system").toString());
  accent_ = s.value("appearance/accent", "ocean").toString();
  detect_krita_brushes_ = s.value("resources/detect_krita_brushes", false).toBool();
  pressure_curve_ = s.value("tablet/pressure_curve", "0.0,0.0;1.0,1.0").toString();
  const QString s_mode = s.value("startup/mode",
                         s.value("General/startup_mode",
                         s.value("general/startup_mode", "gallery"))).toString();
  startup_mode_ = (s_mode == "blank_canvas") ? StartupMode::BLANK_CANVAS : StartupMode::GALLERY;
  const QString def_proj = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Kreart Projects";
  projects_directory_ = s.value("startup/projects_directory",
                        s.value("General/projects_directory",
                        s.value("general/projects_directory", def_proj))).toString();
  grid_size_ = s.value("grid/size", 32).toInt();
  grid_color_ = QColor(s.value("grid/color", "rgba(128,128,140,90)").toString());
  if (!grid_color_.isValid()) grid_color_ = QColor(128, 128, 140, 90);
  show_grid_ = s.value("grid/show", false).toBool();
  transparency_follow_device_ = s.value("transparency/follow_device", true).toBool();
  transparency_mode_ = static_cast<TransparencyMode>(
      s.value("transparency/mode", static_cast<int>(TransparencyMode::DARK_CHECKERBOARD)).toInt());
  const QString trans_color_str = s.value("transparency/solid_color", "#ff202124").toString();
  transparency_solid_color_ = QColor(trans_color_str);
  if (!transparency_solid_color_.isValid()) transparency_solid_color_ = QColor(32, 33, 36);
  recent_files_ = s.value("files/recent").toStringList();
}

void AppSettings::save() const {
  QSettings s("io.github.kreart", "Kreart");
  s.setValue("appearance/theme", theme_to_string(theme_));
  s.setValue("appearance/accent", accent_);
  s.setValue("resources/detect_krita_brushes", detect_krita_brushes_);
  s.setValue("tablet/pressure_curve", pressure_curve_);
  const QString mode_str = (startup_mode_ == StartupMode::BLANK_CANVAS) ? "blank_canvas" : "gallery";
  s.setValue("startup/mode", mode_str);
  s.setValue("General/startup_mode", mode_str);
  s.setValue("startup/projects_directory", projects_directory_);
  s.setValue("General/projects_directory", projects_directory_);
  s.setValue("grid/size", grid_size_);
  s.setValue("grid/color", QString("rgba(%1,%2,%3,%4)").arg(grid_color_.red()).arg(grid_color_.green()).arg(grid_color_.blue()).arg(grid_color_.alpha()));
  s.setValue("grid/show", show_grid_);
  s.setValue("transparency/follow_device", transparency_follow_device_);
  s.setValue("transparency/mode", static_cast<int>(transparency_mode_));
  s.setValue("transparency/solid_color", transparency_solid_color_.name(QColor::HexArgb));
  s.setValue("files/recent", recent_files_);
  s.sync();
}

void AppSettings::add_recent_file(const QString &path) {
  if (path.isEmpty()) return;
  QStringList next = recent_files_;
  next.removeAll(path);
  next.prepend(path);
  while (next.size() > 10) next.removeLast();
  if (next == recent_files_) return;
  recent_files_ = next;
  save();
  emit recentFilesChanged();
}

void AppSettings::clear_recent_files() {
  if (recent_files_.isEmpty()) return;
  recent_files_.clear();
  save();
  emit recentFilesChanged();
}

QKeySequence AppSettings::shortcut(const QString &id, const QKeySequence &fallback) const {
  QSettings s("io.github.kreart", "Kreart");
  const QString key = "shortcuts/" + id;
  const QVariant v = s.value(key);
  if (!v.isValid() || v.toString().isEmpty()) {
    return fallback;
  }
  return QKeySequence(v.toString());
}

void AppSettings::set_shortcut(const QString &id, const QKeySequence &seq) {
  QSettings s("io.github.kreart", "Kreart");
  const QString key = "shortcuts/" + id;
  const QString text = seq.toString();
  if (s.value(key).toString() == text) {
    return;
  }
  s.setValue(key, text);
  s.sync();
  emit shortcutsChanged();
}

void AppSettings::clear_shortcuts() {
  QSettings s("io.github.kreart", "Kreart");
  s.remove("shortcuts");
  s.sync();
  emit shortcutsChanged();
}

bool AppSettings::export_shortcuts(const QString &path) const {
  QSettings src("io.github.kreart", "Kreart");
  QSettings dst(path, QSettings::IniFormat);
  dst.clear();
  src.beginGroup("shortcuts");
  dst.beginGroup("shortcuts");
  for (const QString &id : src.childKeys()) {
    dst.setValue(id, src.value(id));
  }
  dst.endGroup();
  src.endGroup();
  dst.sync();
  return dst.status() == QSettings::NoError;
}

bool AppSettings::import_shortcuts(const QString &path) {
  QSettings dst("io.github.kreart", "Kreart");
  dst.remove("shortcuts");
  QSettings src(path, QSettings::IniFormat);
  src.beginGroup("shortcuts");
  dst.beginGroup("shortcuts");
  for (const QString &id : src.childKeys()) {
    dst.setValue(id, src.value(id));
  }
  dst.endGroup();
  src.endGroup();
  dst.sync();
  emit shortcutsChanged();
  return dst.status() == QSettings::NoError;
}

} // namespace kreart::ui
