#pragma once

#include <QStackedWidget>
#include <QWidget>

#include <filesystem>

#include "kreart/ui/mobile/mobile_gallery.h"
#include "kreart/ui/mobile_editor_view.h"

namespace kreart::ui {

// Root window for the Android / mobile UI mode. Hosts the mobile gallery and
// the mobile editor, installs the application-wide Back handler and applies the
// Ink & Amber stylesheet.
class AndroidAppWindow : public QWidget {
  Q_OBJECT

public:
  explicit AndroidAppWindow(QWidget *parent = nullptr);
  ~AndroidAppWindow() override;

  void open_file(const std::filesystem::path &path);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  bool eventFilter(QObject *obj, QEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void setup_connections();
  void go_to_gallery();
  void go_to_editor();

  QStackedWidget *stack_ = nullptr;
  mobile::MobileGallery *gallery_view_ = nullptr;
  MobileEditorView *editor_view_ = nullptr;
  bool startup_applied_ = false;
};

} // namespace kreart::ui
