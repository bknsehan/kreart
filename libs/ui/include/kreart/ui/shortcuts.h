#pragma once

#include <QKeySequence>
#include <vector>

namespace kreart::ui {

// One configurable keyboard shortcut. `id` is the stable key used for
// persistence (QSettings "shortcuts/<id>" and profile files); `default_seq`
// is the built-in binding restored by "Reset to Defaults".
struct ShortcutDef {
  const char *id;
  const char *label;
  QKeySequence default_seq;
};

// The full set of shortcut-configurable actions, in display order. Every
// entry must have a matching QAction created by MainWindow::setup_actions
// and registered in action_by_id_.
const std::vector<ShortcutDef> &default_shortcuts();

// Look up a label by id (for profile files / settings UI).
QString shortcut_label(const char *id);

} // namespace kreart::ui
