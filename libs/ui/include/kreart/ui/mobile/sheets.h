#pragma once

#include <functional>

#include <QHash>
#include <QString>
#include <QWidget>

#include "kreart/ui/mobile/editor_context.h"

namespace kreart::ui::mobile {

class MobileSheet;

// Owns every sheet belonging to one screen and enforces the
// exactly-one-open invariant. Sheets are built lazily on first use and cached.
//
// Sheet ids: "hub", "color", "brushes", "layers", "tool", "selection",
// "image", "filters", "view", "document", "settings".
class EditorSheets : public QObject {
public:
  EditorSheets(QWidget *host, EditorContext ctx, QObject *parent = nullptr);

  // Builds (if needed) and opens the sheet with the given id.
  MobileSheet *show(const QString &id);
  // Closes the active sheet, if any. Returns true when something was closed.
  bool close_active();
  bool has_active() const { return active_ != nullptr; }
  MobileSheet *active() const { return active_; }
  MobileSheet *peek(const QString &id) const { return sheets_.value(id, nullptr); }
  void relayout();

  // Convenience sheet builders reused by several sheets.
  MobileSheet *make_value_sheet(const QString &title, const QString &label,
                                double min_value, double max_value, double initial,
                                const QString &suffix,
                                std::function<void(double)> on_apply);
  MobileSheet *make_text_sheet(const QString &title, const QString &label,
                               const QString &initial,
                               std::function<bool(const QString &, QString &)> on_accept);
  MobileSheet *make_confirm_sheet(const QString &title, const QString &message,
                                  const QString &confirm_label, bool danger,
                                  std::function<void()> on_confirm);

  EditorContext &context() { return ctx_; }
  QWidget *host() const { return host_; }

  // Ids of transient (non-cached) sheets get cleaned up when closed.
  void register_transient(MobileSheet *sheet);
  // Shows a transient sheet built outside this registry, closing the previous.
  void show_sheet_public(MobileSheet *sheet) { show_sheet(sheet); }

private:
  MobileSheet *build(const QString &id);
  void bind(MobileSheet *sheet);
  void show_sheet(MobileSheet *sheet);

  QWidget *host_ = nullptr;
  EditorContext ctx_;
  QHash<QString, MobileSheet *> sheets_;
  MobileSheet *active_ = nullptr;
};

} // namespace kreart::ui::mobile
