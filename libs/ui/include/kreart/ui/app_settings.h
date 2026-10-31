#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QKeySequence>
#include <QColor>
#include <string_view>
#include <vector>

namespace kreart::ui {

enum class ThemeMode {
  SYSTEM,
  DARK,
  LIGHT
};

enum class StartupMode {
  GALLERY,
  BLANK_CANVAS
};
enum class TransparencyMode {
  LIGHT_CHECKERBOARD,
  DARK_CHECKERBOARD,
  SOLID_COLOR
};

struct AccentPalette {
  const char *id = nullptr;
  const char *label = nullptr;
  const char *dark_accent = nullptr;
  const char *dark_hover = nullptr;
  const char *dark_soft = nullptr;
  const char *dark_active_bg = nullptr;
  const char *dark_fg = nullptr;
  const char *light_accent = nullptr;
  const char *light_hover = nullptr;
  const char *light_soft = nullptr;
  const char *light_active_bg = nullptr;
  const char *light_fg = nullptr;
};

class AppSettings : public QObject {
  Q_OBJECT

public:
  static AppSettings &instance();

  static const std::vector<AccentPalette> &accent_palettes();
  static const AccentPalette *accent_from_id(std::string_view id);

  ThemeMode theme() const { return theme_; }
  void set_theme(ThemeMode mode);

  QString accent() const { return accent_; }
  void set_accent(const QString &id);

  bool detect_krita_brushes() const { return detect_krita_brushes_; }
  void set_detect_krita_brushes(bool enabled);

  QString pressure_curve() const { return pressure_curve_; }
  void set_pressure_curve(const QString &curve);

  QStringList recent_files() const { return recent_files_; }
  void add_recent_file(const QString &path);
  void clear_recent_files();

  // Configurable keyboard shortcuts. Bindings live under QSettings
  // "shortcuts/<id>"; absent keys fall back to the action's default sequence.
  QKeySequence shortcut(const QString &id, const QKeySequence &fallback) const;
  void set_shortcut(const QString &id, const QKeySequence &seq);
  void clear_shortcuts();
  // Profile files (.ksf) are INI files mapping action-id -> keysequence text.
  bool export_shortcuts(const QString &path) const;
  bool import_shortcuts(const QString &path);

  StartupMode startup_mode() const { return startup_mode_; }
  void set_startup_mode(StartupMode mode);

  QString projects_directory() const;
  void set_projects_directory(const QString &dir);

  int grid_size() const { return grid_size_; }
  void set_grid_size(int size);

  QColor grid_color() const { return grid_color_; }
  void set_grid_color(const QColor &color);

  bool show_grid() const { return show_grid_; }
  void set_show_grid(bool show);
  bool transparency_follow_device() const { return transparency_follow_device_; }
  void set_transparency_follow_device(bool follow);

  TransparencyMode transparency_mode() const { return transparency_mode_; }
  void set_transparency_mode(TransparencyMode mode);

  QColor transparency_solid_color() const { return transparency_solid_color_; }
  void set_transparency_solid_color(const QColor &color);

  TransparencyMode effective_transparency_mode(bool is_dark_theme) const {
    if (transparency_follow_device_) {
      return is_dark_theme ? TransparencyMode::DARK_CHECKERBOARD : TransparencyMode::LIGHT_CHECKERBOARD;
    }
    return transparency_mode_;
  }
  void load();
  void save() const;

  static QString theme_to_string(ThemeMode mode);
  static ThemeMode theme_from_string(const QString &str);

signals:
  void themeChanged(ThemeMode mode);
  void accentChanged(const QString &id);
  void detectKritaBrushesChanged(bool enabled);
  void pressureCurveChanged(const QString &curve);
  void recentFilesChanged();
  void shortcutsChanged();

  void startupModeChanged(StartupMode mode);
  void projectsDirectoryChanged(const QString &dir);
  void gridSettingsChanged();
  void transparencySettingsChanged();
private:
  AppSettings();
  ThemeMode theme_ = ThemeMode::SYSTEM;
  QString accent_ = "ocean";
  bool detect_krita_brushes_ = false;
  QString pressure_curve_ = "0.0,0.0;1.0,1.0";
  QStringList recent_files_;
  StartupMode startup_mode_ = StartupMode::GALLERY;
  QString projects_directory_;
  int grid_size_ = 32;
  QColor grid_color_{128, 128, 140, 90};
  bool show_grid_ = false;
  bool transparency_follow_device_ = true;
  TransparencyMode transparency_mode_ = TransparencyMode::DARK_CHECKERBOARD;
  QColor transparency_solid_color_{32, 33, 36};
};

} // namespace kreart::ui
