#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <QString>
#include <QObject>

class QWidget;

// Android-only platform helpers (file pickers, projects root). Non-Android
// builds get safe fallbacks so the `-m` desktop preview still compiles and runs.
namespace kreart::mobile {

// Opens the system document picker. Returns a local file path (the content URI
// is copied into the app cache because io::read_kra/write_png need a real path),
// or an empty string when the user cancels.
QString pick_file(QWidget *parent, const QString &mime, const QString &title);

// Opens the system folder picker. Returns an empty string when cancelled.
QString pick_folder(QWidget *parent);

// The single projects root shared by MobileGallery and MobileEditorView.
std::filesystem::path mobile_projects_dir();

// True when `stem` is a legal file name stem (no path separators or reserved
// characters) for use by the gallery and Save As sheets.
bool is_valid_file_stem(const QString &stem, QString &error);

// Appends " 2", " 3", ... until `<stem><ext>` does not exist in `dir`.
std::filesystem::path unique_path_in(const std::filesystem::path &dir,
                                     const std::string &stem,
                                     const std::string &extension);

} // namespace kreart::mobile
