#pragma once

#include <QMainWindow>
#include <filesystem>
#include "kreart/ui/gallery_view.h"

namespace kreart::ui {

class MainWindow;

class ProjectGalleryWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit ProjectGalleryWindow(MainWindow *main_window = nullptr, QWidget *parent = nullptr);
  ~ProjectGalleryWindow() override = default;

  GalleryView *gallery_view() const { return gallery_view_; }
  void refresh();

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void open_project(const std::filesystem::path &path);
  void create_new_project(int width, int height, const QString &title);
  void open_blank_canvas();
  void import_project();

  MainWindow *main_window_ = nullptr;
  GalleryView *gallery_view_ = nullptr;
};

} // namespace kreart::ui
